/*
 * Copyright (c) 2025-2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "cef/ohos_cef_ext/libcef/browser/arkweb_error_page_auto_reloader.h"

#include <algorithm>
#include <array>

#include "base/functional/callback.h"
#include "base/logging.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/network_service_instance.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "url/gurl.h"

#include "arkweb/chromium_ext/url/ohos/log_utils.h"
#if BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
#include "arkweb/chromium_ext/content/public/common/content_switches_ext.h"
#if BUILDFLAG(IS_ARKWEB_EXT)
#include "arkweb/ohos_nweb_ex/overrides/net/proxy_resolution/fallback_proxy_utils.h"
#endif
#include "base/command_line.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/navigation_controller.h"
#endif

namespace OHOS::NWeb {

namespace {

// Maximum number of generic auto-reload attempts for a single error before
// giving up. Note: upstream NetErrorAutoReloader retries indefinitely (delay
// capped at 30 minutes); ArkWeb stops after 7 attempts (cumulative maximum
// wait ~46m36s, i.e. 2796 seconds).
constexpr size_t kMaxAutoReloadCount = 7;

// Aligned with upstream error_page::NetErrorAutoReloader::GetNextReloadDelay.
base::TimeDelta GetNextReloadDelay(size_t reload_count) {
  constexpr static const auto kDelays = std::to_array<base::TimeDelta>({
      base::Seconds(1),
      base::Seconds(5),
      base::Seconds(30),
      base::Minutes(1),
      base::Minutes(5),
      base::Minutes(10),
      base::Minutes(30),
  });
  static_assert(std::size(kDelays) == kMaxAutoReloadCount,
                "kMaxAutoReloadCount must match kDelays array size");
  return kDelays[std::min(reload_count, std::size(kDelays) - 1)];
}

#if BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
content::ErrorPageReloadReason SetErrorPageReloadReason(
    content::NavigationHandle* handle,
    int net_error) {
  if (handle->NeedsReloadWithFallbackProxy()) {
    return content::ErrorPageReloadReason::FALLBACK_PROXY;
  }
  return content::ErrorPageReloadReason::INVALID;
}
#endif

// Helper to block a navigation that would result in re-committing the same
// error page a tab is already displaying.
class IgnoreDuplicateErrorThrottle : public content::NavigationThrottle {
 public:
  using ShouldSuppressCallback =
      base::OnceCallback<bool(content::NavigationHandle*)>;

  IgnoreDuplicateErrorThrottle(content::NavigationThrottleRegistry& registry,
                               ShouldSuppressCallback should_suppress)
      : content::NavigationThrottle(registry),
        should_suppress_(std::move(should_suppress)) {
    DCHECK(should_suppress_);
  }
  IgnoreDuplicateErrorThrottle(const IgnoreDuplicateErrorThrottle&) = delete;
  IgnoreDuplicateErrorThrottle& operator=(const IgnoreDuplicateErrorThrottle&) =
      delete;
  ~IgnoreDuplicateErrorThrottle() override = default;

  content::NavigationThrottle::ThrottleCheckResult WillFailRequest() override {
    DCHECK(should_suppress_);
    if (std::move(should_suppress_).Run(navigation_handle()))
      return content::NavigationThrottle::ThrottleAction::CANCEL;
    return content::NavigationThrottle::ThrottleAction::PROCEED;
  }

  const char* GetNameForLogging() override {
    return "IgnoreDuplicateErrorThrottle";
  }

 private:
  ShouldSuppressCallback should_suppress_;
};

}  // namespace

ErrorPageAutoReloader::ErrorPageInfo::ErrorPageInfo(const GURL& url,
                                                   net::Error error)
    : url(url), error(error) {}

ErrorPageAutoReloader::ErrorPageInfo::~ErrorPageInfo() = default;

ErrorPageAutoReloader::ErrorPageAutoReloader(content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<ErrorPageAutoReloader>(*web_contents),
      connection_tracker_(content::GetNetworkConnectionTracker()) {
  connection_tracker_->AddNetworkConnectionObserver(this);

  network::mojom::ConnectionType connection_type;
  if (connection_tracker_->GetConnectionType(
          &connection_type,
          base::BindOnce(&ErrorPageAutoReloader::SetInitialConnectionType,
                         weak_ptr_factory_.GetWeakPtr()))) {
    SetInitialConnectionType(connection_type);
  }
}

ErrorPageAutoReloader::~ErrorPageAutoReloader() {
  if (connection_tracker_)
    connection_tracker_->RemoveNetworkConnectionObserver(this);
}

// static
void ErrorPageAutoReloader::MaybeCreateAndAddNavigationThrottle(
    content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  if (!handle.IsInPrimaryMainFrame()) {
    return;
  }

  // Note that `CreateForWebContents` is a no-op if `contents` already has a
  // ErrorPageAutoReloader. See WebContentsUserData.
  content::WebContents* contents = handle.GetWebContents();
  CreateForWebContents(contents);
  FromWebContents(contents)->MaybeCreateAndAdd(registry);
}

void ErrorPageAutoReloader::DidStartNavigation(
    content::NavigationHandle* handle) {
  if (!handle->IsInPrimaryMainFrame())
    return;

  // Suppress automatic reload as long as any navigations are pending.
  PauseAutoReloadTimerIfRunning();
  pending_navigations_.insert(handle);
}

void ErrorPageAutoReloader::DidFinishNavigation(
    content::NavigationHandle* handle) {
  if (!handle->IsInPrimaryMainFrame())
    return;

#if BUILDFLAG(IS_ARKWEB_EXT) && BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
  if (handle->GetCurrentReloadReason() !=
      content::ErrorPageReloadReason::INVALID) {
    ReportFallProxyReloadErrorCodeInfo(
        handle->GetURL().spec(), (int)handle->GetCurrentReloadReason(),
        handle->GetOriginalNetErrorCode(), handle->GetNetErrorCode());
  }
#endif

  pending_navigations_.erase(handle);
  if (!handle->HasCommitted()) {
    // This navigation was cancelled and not committed. If there are still other
    // pending navigations, or we aren't sitting on a error page which allows
    // auto-reload, there's nothing to do.
    if (!pending_navigations_.empty() || !current_reloadable_error_page_info_)
      return;

#if BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          ::switches::kEnableNwebEx) &&
        current_reloadable_error_page_info_ &&
        current_reloadable_error_page_info_->reason >
            content::ErrorPageReloadReason::INVALID &&
        handle->HasBeenReloadedForThisReason(
            current_reloadable_error_page_info_->reason)) {
      return;
    }
#endif

    // The last pending navigation was just cancelled and we're sitting on an
    // error page which allows auto-reload. Schedule the next auto-reload
    // attempt.
    is_auto_reload_in_progress_ = false;
    ScheduleNextAutoReload();
    return;
  }

  if (!ShouldAutoReload(handle)) {
    if (!auto_reload_enabled_ && IsReloadableError(handle)) {
      // Auto reload is disabled but the error is otherwise reloadable.
      // Preserve error page info so the timer can be resumed if the user
      // later enables auto reload, but don't schedule a timer now.
      net::Error net_error = handle->GetNetErrorCode();
      if (handle->GetReloadType() == content::ReloadType::NONE ||
          !current_reloadable_error_page_info_ ||
          net_error != current_reloadable_error_page_info_->error) {
        num_reloads_for_current_error_ = 0;
        is_auto_reload_in_progress_ = false;
      }
      current_reloadable_error_page_info_ =
          ErrorPageInfo(handle->GetURL(), net_error);
#if BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
      current_reloadable_error_page_info_->reason =
          SetErrorPageReloadReason(handle, net_error);
#endif
      return;
    }
    // We've committed something that doesn't support auto-reload. Reset
    // all auto-reload state so nothing interesting happens until another
    // error page navigation is committed.
    Reset();
    return;
  }

  // This heuristic isn't perfect but it should be good enough: if the new
  // commit is not a reload, or if it's an error page with an error code
  // different from what we had previously committed, we treat it as a new
  // error and thus reset our tracking state.
  net::Error net_error = handle->GetNetErrorCode();
  if (handle->GetReloadType() == content::ReloadType::NONE ||
      !current_reloadable_error_page_info_ ||
      net_error != current_reloadable_error_page_info_->error) {
    Reset();
    current_reloadable_error_page_info_ =
        ErrorPageInfo(handle->GetURL(), net_error);
  }

#if BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
  current_reloadable_error_page_info_->reason =
      SetErrorPageReloadReason(handle, net_error);
#endif

  // We only schedule a reload if there are no other pending navigations.
  // If there are and they end up getting terminated without a commit, we
  // will schedule the next auto-reload at that time.
  if (pending_navigations_.empty())
    ScheduleNextAutoReload();
}

void ErrorPageAutoReloader::NavigationStopped() {
  // Stopping navigation or loading cancels all pending auto-reload behavior
  // until the next time a new error page is committed. Note that a stop during
  // navigation will also result in a DidFinishNavigation with a failed
  // navigation and an error code of ERR_ABORTED. However stops can also occur
  // after an error page commits but before it finishes loading, and we want to
  // catch those cases too.
  Reset();
}

void ErrorPageAutoReloader::OnVisibilityChanged(content::Visibility visibility) {
  if (!IsWebContentsVisible()) {
    PauseAutoReloadTimerIfRunning();
  } else if (pending_navigations_.empty()) {
    ResumeAutoReloadIfPaused();
  }
}

void ErrorPageAutoReloader::OnConnectionChanged(
    network::mojom::ConnectionType type) {
  is_online_ = (type != network::mojom::ConnectionType::CONNECTION_NONE);
  if (!is_online_) {
    PauseAutoReloadTimerIfRunning();
  } else if (pending_navigations_.empty()) {
    ResumeAutoReloadIfPaused();
  }
}

// static
base::TimeDelta ErrorPageAutoReloader::GetNextReloadDelayForTesting(
    size_t reload_count) {
  return GetNextReloadDelay(reload_count);
}

void ErrorPageAutoReloader::DisableConnectionChangeObservationForTesting() {
  if (connection_tracker_) {
    connection_tracker_->RemoveNetworkConnectionObserver(this);
    connection_tracker_ = nullptr;
  }
}

void ErrorPageAutoReloader::SetAutoReloadEnabled(bool enable) {
  auto_reload_enabled_ = enable;
  if (enable) {
    // Re-enabling: resume the auto-reload timer if we're sitting on an
    // error page and no timer is currently scheduled. This handles the
    // disable->re-enable scenario where PauseAutoReloadTimerIfRunning()
    // destroyed the timer.
    if (pending_navigations_.empty()) {
      ResumeAutoReloadIfPaused();
    }
    return;
  }
#if BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
  if (current_reloadable_error_page_info_ &&
      current_reloadable_error_page_info_->reason !=
          content::ErrorPageReloadReason::INVALID) {
    // Never disturb a pending fallback proxy auto reload timer.
    return;
  }
#endif
  PauseAutoReloadTimerIfRunning();
}

bool ErrorPageAutoReloader::IsReloadableError(
    content::NavigationHandle* handle) {
  const int net_error = handle->GetNetErrorCode();
  return handle->IsErrorPage() && net_error != net::OK && !handle->IsPost() &&
         net_error != net::ERR_UNKNOWN_URL_SCHEME &&
         !net::IsCertificateError(net_error) &&
         !net::IsClientCertificateError(net_error) &&
         net_error != net::ERR_SSL_PROTOCOL_ERROR &&
         !net::IsRequestBlockedError(net_error) &&
         net_error != net::ERR_INVALID_AUTH_CREDENTIALS &&
         handle->GetURL().SchemeIsHTTPOrHTTPS() &&
         !handle->GetResolveErrorInfo().is_secure_network_error &&
         net_error != net::ERR_HTTP_RESPONSE_CODE_FAILURE &&
         net_error != net::ERR_BLOCKED_BY_LOCAL_NETWORK_ACCESS_CHECKS;
}

bool ErrorPageAutoReloader::ShouldAutoReload(
    content::NavigationHandle* handle) {
  DCHECK(handle->HasCommitted());
#if BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          ::switches::kEnableNwebEx) &&
      handle->NeedsReloadWithFallbackProxy()) {
    return true;
  }
#endif

  if (!auto_reload_enabled_) {
    return false;
  }
#if BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
  if (handle->NeedsReloadWithFallbackProxy()) {
    // Fallback proxy error pages are only handled by the fallback proxy branch
    // above, never by the generic auto reload.
    return false;
  }
#endif

  return IsReloadableError(handle);
}

void ErrorPageAutoReloader::SetInitialConnectionType(
    network::mojom::ConnectionType type) {
  if (connection_tracker_)
    OnConnectionChanged(type);
}

bool ErrorPageAutoReloader::IsWebContentsVisible() {
  return web_contents()->GetVisibility() != content::Visibility::HIDDEN;
}

void ErrorPageAutoReloader::Reset() {
  next_reload_timer_.reset();
  num_reloads_for_current_error_ = 0;
  is_auto_reload_in_progress_ = false;
  current_reloadable_error_page_info_.reset();
}

void ErrorPageAutoReloader::PauseAutoReloadTimerIfRunning() {
  next_reload_timer_.reset();
}

void ErrorPageAutoReloader::ResumeAutoReloadIfPaused() {
  if (current_reloadable_error_page_info_ && !next_reload_timer_)
    ScheduleNextAutoReload();
}

void ErrorPageAutoReloader::ScheduleNextAutoReload() {
  DCHECK(current_reloadable_error_page_info_);
  if (!is_online_ || !IsWebContentsVisible())
    return;

#if BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          ::switches::kEnableNwebEx) &&
      current_reloadable_error_page_info_->reason ==
          content::ErrorPageReloadReason::FALLBACK_PROXY) {
    // Existing fallback proxy behavior, kept unchanged: fixed 1s delay, single
    // attempt (guaranteed by HasBeenReloadedForThisReason in
    // DidFinishNavigation).
    next_reload_timer_.emplace();
    next_reload_timer_->Start(
        FROM_HERE, base::Seconds(1),
        base::BindOnce(&ErrorPageAutoReloader::ReloadMainFrame,
                       weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  if (current_reloadable_error_page_info_->reason !=
      content::ErrorPageReloadReason::INVALID) {
    // Fallback proxy related error page: never handled by the generic auto
    // reload below.
    return;
  }
#endif

  // Generic network error auto reload: backoff delay sequence with a maximum
  // of kMaxAutoReloadCount attempts for a single error.
  if (!auto_reload_enabled_) {
    return;
  }
  if (num_reloads_for_current_error_ >= kMaxAutoReloadCount) {
    return;
  }

  next_reload_timer_.emplace();
  next_reload_timer_->Start(
      FROM_HERE, GetNextReloadDelay(num_reloads_for_current_error_),
      base::BindOnce(&ErrorPageAutoReloader::ReloadMainFrame,
                     weak_ptr_factory_.GetWeakPtr()));
}

void ErrorPageAutoReloader::ReloadMainFrame() {
  DCHECK(current_reloadable_error_page_info_);
  if (!is_online_ || !IsWebContentsVisible())
    return;

#if BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
  if (current_reloadable_error_page_info_->reason ==
      content::ErrorPageReloadReason::FALLBACK_PROXY) {
    // Existing fallback proxy reload behavior, kept unchanged.
    is_auto_reload_in_progress_ = true;
    LOG(INFO) << "[ErrReload] Net error auto reloader --url: "
              << url::LogUtils::ConvertUrlWithMask(
                     current_reloadable_error_page_info_->url.spec())
              << " error code: " << current_reloadable_error_page_info_->error
              << ", reason " << (int)current_reloadable_error_page_info_->reason;
    web_contents()->GetController().ReloadWithNetError(
        content::ReloadType::NORMAL, true,
        current_reloadable_error_page_info_->reason);
    return;
  }
#endif

  // Generic network error auto reload, aligned with upstream
  // error_page::NetErrorAutoReloader::ReloadMainFrame().
  ++num_reloads_for_current_error_;
  is_auto_reload_in_progress_ = true;
  LOG(INFO) << "[ErrReload] Generic auto reload --url: "
            << url::LogUtils::ConvertUrlWithMask(
                   current_reloadable_error_page_info_->url.spec())
            << " error code: " << current_reloadable_error_page_info_->error
            << " reload count: " << num_reloads_for_current_error_;
  auto* main_frame = web_contents()->GetPrimaryMainFrame();
  if (main_frame) {
    main_frame->Reload();
  } else {
    LOG(WARNING) << "[ErrReload] GetPrimaryMainFrame() returned null, "
                 << "skipping reload for url: "
                 << url::LogUtils::ConvertUrlWithMask(
                        current_reloadable_error_page_info_->url.spec());
  }
}

void ErrorPageAutoReloader::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  DCHECK(handle.IsInPrimaryMainFrame());
  if (!current_reloadable_error_page_info_ ||
      current_reloadable_error_page_info_->url != handle.GetURL() ||
      !is_auto_reload_in_progress_) {
    return;
  }

  registry.AddThrottle(std::make_unique<IgnoreDuplicateErrorThrottle>(
      registry, base::BindOnce(&ErrorPageAutoReloader::ShouldSuppressErrorPage,
                               base::Unretained(this))));
}

bool ErrorPageAutoReloader::ShouldSuppressErrorPage(
    content::NavigationHandle* handle) {
  // We already verified these conditions when the throttle was created, but now
  // that the throttle is about to fail its navigation, we double-check in case
  // another navigation has committed in the interim.
  if (!current_reloadable_error_page_info_ ||
      current_reloadable_error_page_info_->url != handle->GetURL() ||
      current_reloadable_error_page_info_->error != handle->GetNetErrorCode()) {
    return false;
  }

#if BUILDFLAG(ARKWEB_EX_FALLBACK_PROXY)
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          ::switches::kEnableNwebEx) &&
      current_reloadable_error_page_info_->reason !=
      content::ErrorPageReloadReason::INVALID) {
    return false;
  }
#endif

  return true;
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(ErrorPageAutoReloader);

}  // namespace OHOS::NWeb
