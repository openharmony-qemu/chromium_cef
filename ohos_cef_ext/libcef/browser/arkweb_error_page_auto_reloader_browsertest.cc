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

// Browser tests for ErrorPageAutoReloader, ported from upstream Chromium
// net_error_auto_reloader_browsertest.cc (144.0.7559.59 baseline) with
// ArkWeb extensions:
//   - 16 cases aligned with upstream (gated by the auto reload switch, which
//     upstream does not have),
//   - 3 cases for the NWeb switch gating,
//   - 5 cases for fallback proxy isolation (reason field + switch),
//   - 1 case for the 7-attempt backoff cap (ArkWeb deviation from upstream),
//   - 8 cases for error code filtering.
//
// Note: these are content::ContentBrowserTest cases. They must be run under a
// browser-test main (e.g. a content_browsertests-style binary with
// content::Shell available); registering the file in a plain run_all_unittests
// target only validates compilation.

#ifdef UNSAFE_BUFFERS_BUILD
// TODO(crbug.com/40285824): Remove this and convert code to safer constructs.
#pragma allow_unsafe_buffers
#endif

#include "cef/ohos_cef_ext/libcef/browser/arkweb_error_page_auto_reloader.h"

#include <memory>
#include <optional>
#include <utility>

#include "base/functional/bind.h"
#include "base/command_line.h"
#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/public/test/content_browser_test_utils.h"
#include "content/public/test/url_loader_interceptor.h"
#include "content/shell/browser/shell.h"
#include "content/public/test/test_navigation_throttle_inserter.h"
#include "content/shell/browser/shell_content_browser_client.h"
#include "net/base/net_errors.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace OHOS::NWeb {
namespace {

// Intercepts requests for a URL and fails them with a net error.
class NetErrorUrlInterceptor {
 public:
  // ArkWeb: binds a URL-matching interceptor that fails the request.
  NetErrorUrlInterceptor(GURL url, net::Error error)
      : url_(std::move(url)),
        interceptor_(base::BindLambdaForTesting(
            [this,
             error](content::URLLoaderInterceptor::RequestParams* params) {
              // ArkWeb: skip non-matching URLs.
              if (params->url_request.url != url_) {
                return false;
              }
              // ArkWeb: terminate the intercepted request with the error.
              network::URLLoaderCompletionStatus status;
              status.error_code = error;
              // ArkWeb: deliver the completion status to the client.
              params->client->OnComplete(status);
              return true;
            })) {}
  ~NetErrorUrlInterceptor() = default;

 // ArkWeb: private init.
 private:
  const GURL url_;
  const content::URLLoaderInterceptor interceptor_;
};

// Defers the first navigation after construction for test control.
class DeferNextNavigationThrottleInserter
    : public content::WebContentsObserver {
 public:
  class DeferringThrottle : public content::NavigationThrottle {
   public:
    // ArkWeb: construct with a quit callback for deferral signaling.
    explicit DeferringThrottle(content::NavigationThrottleRegistry& registry,
                               base::OnceClosure callback)
        : NavigationThrottle(registry), callback_(std::move(callback)) {}

    ~DeferringThrottle() override = default;

    void Cancel() { CancelDeferredNavigation(CANCEL); }

    // ArkWeb: defer the request and signal the test.
    ThrottleCheckResult WillStartRequest() override {
      std::move(callback_).Run();
      return DEFER;
    }
    // ArkWeb: logging identifier for this throttle.
    const char* GetNameForLogging() override { return "DeferringThrottle"; }

   private:
    base::OnceClosure callback_;
  };

  // ArkWeb: observe web contents and insert a deferring throttle.
  explicit DeferNextNavigationThrottleInserter(
      content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents),
        throttle_inserter_(
            web_contents,
            // ArkWeb: bind the throttle creation callback.
            base::BindRepeating(
                &DeferNextNavigationThrottleInserter::MaybeCreateThrottle,
                base::Unretained(this))) {}
  ~DeferNextNavigationThrottleInserter() override = default;

  // ArkWeb: blocks until the next navigation is deferred.
  void WaitForNextNavigationToBeDeferred() { defer_wait_loop_.Run(); }

  // ArkWeb: cancel the deferred navigation and wait for completion.
  void CancelAndWaitForNavigationToFinish() {
    throttle_->Cancel();
    finish_wait_loop_.Run();
  }

  void DidFinishNavigation(content::NavigationHandle* handle) override {
    DCHECK(throttle_);
    if (handle == throttle_->navigation_handle()) {
      finish_wait_loop_.Quit();
    }
  }

 private:
  void MaybeCreateThrottle(content::NavigationThrottleRegistry& registry) {
    if (throttle_) {
      return;
    }

    // ArkWeb: create the deferring throttle and register it.
    auto throttle = std::make_unique<DeferringThrottle>(
        registry, defer_wait_loop_.QuitClosure());
    throttle_ = throttle.get();
    registry.AddThrottle(std::move(throttle));
  }

  const content::TestNavigationThrottleInserter throttle_inserter_;
  raw_ptr<DeferringThrottle, AcrossTasksDanglingUntriaged> throttle_ = nullptr;
  base::RunLoop defer_wait_loop_;
  base::RunLoop finish_wait_loop_;
};

base::TimeDelta GetDelayForReloadCount(size_t count) {
  return ErrorPageAutoReloader::GetNextReloadDelayForTesting(count);
}

class ErrorPageAutoReloaderBrowserTest : public content::ContentBrowserTest {
 public:
  void SetUpOnMainThread() override {
    ASSERT_TRUE(embedded_test_server()->Start());
    ErrorPageAutoReloader::CreateForWebContents(web_contents());

    // ArkWeb: start online; the switch defaults to off.
    SimulateNetworkGoingOnline();

    content::ShellContentBrowserClient::Get()
        ->set_create_throttles_for_navigation_callback(base::BindRepeating(
            [](content::NavigationThrottleRegistry& registry) -> void {
              ErrorPageAutoReloader::MaybeCreateAndAddNavigationThrottle(
                  registry);
            }));
  }

  content::WebContents* web_contents() { return shell()->web_contents(); }

  ErrorPageAutoReloader* GetAutoReloader() {
    return ErrorPageAutoReloader::FromWebContents(web_contents());
  }

  // ArkWeb: the auto reload switch is off by default; upstream-aligned cases
  // call this first since upstream has no switch.
  void EnableAutoReload() { GetAutoReloader()->SetAutoReloadEnabled(true); }

  void DisableAutoReload() { GetAutoReloader()->SetAutoReloadEnabled(false); }

  // ArkWeb: returns the delay of the scheduled reload task, or nullopt.
  std::optional<base::TimeDelta> GetCurrentAutoReloadDelay() {
    const std::optional<base::OneShotTimer>& timer =
        GetAutoReloader()->next_reload_timer_for_testing();
    if (!timer) {
      return std::nullopt;
    }
    return timer->GetCurrentDelay();
  }

  GURL GetTestUrl() { return embedded_test_server()->GetURL("/empty.html"); }

  // ArkWeb: waits only for navigation completion (not load stop) so tests can
  // observe immediate side effects like timer scheduling.
  [[nodiscard]] bool NavigateMainFrame(const GURL& url) {
    content::TestNavigationManager navigation(web_contents(), url);
    web_contents()->GetController().LoadURL(url, content::Referrer(),
                                            ui::PAGE_TRANSITION_TYPED,
                                            /*extra_headers=*/std::string());
    (void)navigation.WaitForNavigationFinished();
    return navigation.was_successful();
  }

  void SimulateNetworkGoingOnline() {
    SimulateNetworkGoingOnline(web_contents());
  }
  void SimulateNetworkGoingOffline() {
    SimulateNetworkGoingOffline(web_contents());
  }
  void ForceScheduledAutoReloadNow() {
    ForceScheduledAutoReloadNow(web_contents());
  }

  // ArkWeb: fire the scheduled reload timer immediately.
  static void ForceScheduledAutoReloadNow(content::WebContents* wc) {
    ErrorPageAutoReloader* reloader =
        ErrorPageAutoReloader::FromWebContents(wc);
    std::optional<base::OneShotTimer>& timer =
        reloader->next_reload_timer_for_testing();
    if (timer && timer->IsRunning()) {
      timer->FireNow();
    }
  }

  // ArkWeb: simulate online by sending a WIFI connection event.
  static void SimulateNetworkGoingOnline(content::WebContents* wc) {
    ErrorPageAutoReloader* reloader =
        ErrorPageAutoReloader::FromWebContents(wc);
    reloader->DisableConnectionChangeObservationForTesting();
    reloader->OnConnectionChanged(
        network::mojom::ConnectionType::CONNECTION_WIFI);
  }

  // ArkWeb: simulate offline by sending a NONE connection event.
  static void SimulateNetworkGoingOffline(content::WebContents* wc) {
    ErrorPageAutoReloader* reloader =
        ErrorPageAutoReloader::FromWebContents(wc);
    reloader->DisableConnectionChangeObservationForTesting();
    reloader->OnConnectionChanged(
        network::mojom::ConnectionType::CONNECTION_NONE);
  }
};

// ArkWeb: find the first child frame of a parent.
content::RenderFrameHost* GetChild(content::RenderFrameHost& parent) {
  content::RenderFrameHost* child_rfh = nullptr;
  parent.ForEachRenderFrameHost([&](content::RenderFrameHost* rfh) {
    if (&parent == rfh->GetParent()) {
      CHECK(!child_rfh) << "Multiple children found";
      child_rfh = rfh;
    }
  });
  return child_rfh;
}

// ArkWeb: upstream-aligned cases (16) gated by the auto reload switch.

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest, NoError) {
  EnableAutoReload();
  // ArkWeb: successful navigation schedules no reload.
  EXPECT_TRUE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest, ErrorSchedulesReload) {
  EnableAutoReload();
  // ArkWeb: a normal error page triggers a scheduled reload.
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest, ErrorRecovery) {
  EnableAutoReload();
  auto interceptor = std::make_unique<NetErrorUrlInterceptor>(
      GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
  interceptor.reset();

  // ArkWeb: force the scheduled reload after cancelling interception.
  content::TestNavigationManager navigation(web_contents(), GetTestUrl());
  ForceScheduledAutoReloadNow();
  ASSERT_TRUE(navigation.WaitForNavigationFinished());
  EXPECT_TRUE(navigation.was_successful());

  // ArkWeb: no new auto-reload scheduled after success.
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest, ReloadDelayBackoff) {
  EnableAutoReload();
  auto interceptor = std::make_unique<NetErrorUrlInterceptor>(
      GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // ArkWeb: first failed reload increases delay to count(1).
  {
    content::TestNavigationManager navigation(web_contents(), GetTestUrl());
    ForceScheduledAutoReloadNow();
    ASSERT_TRUE(navigation.WaitForNavigationFinished());
    EXPECT_FALSE(navigation.was_committed());
    EXPECT_EQ(GetDelayForReloadCount(1), GetCurrentAutoReloadDelay());
  }

  // ArkWeb: repeat the backoff step to count(2).
  {
    content::TestNavigationManager navigation(web_contents(), GetTestUrl());
    ForceScheduledAutoReloadNow();
    ASSERT_TRUE(navigation.WaitForNavigationFinished());
    EXPECT_FALSE(navigation.was_committed());
    EXPECT_EQ(GetDelayForReloadCount(2), GetCurrentAutoReloadDelay());
    interceptor.reset();
  }

  // ArkWeb: let the next reload succeed.
  {
    content::TestNavigationManager navigation(web_contents(), GetTestUrl());
    ForceScheduledAutoReloadNow();
    ASSERT_TRUE(navigation.WaitForNavigationFinished());
    EXPECT_TRUE(navigation.was_successful());
  }
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       ResetOnAutoReloadWithNewError) {
  EnableAutoReload();
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
  }

  // ArkWeb: a different error code resets the backoff to the first delay.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_ACCESS_DENIED);
    content::TestNavigationManager navigation(web_contents(), GetTestUrl());
    ForceScheduledAutoReloadNow();
    ASSERT_TRUE(navigation.WaitForNavigationFinished());
    EXPECT_TRUE(navigation.was_committed());
    EXPECT_FALSE(navigation.was_successful());
    EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
  }
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       StopCancelsAutoReload) {
  EnableAutoReload();
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // ArkWeb: start a navigation then Stop() to cancel the pending timer.
  content::TestNavigationManager navigation(web_contents(), GetTestUrl());
  web_contents()->GetController().LoadURL(GetTestUrl(), content::Referrer(),
                                          ui::PAGE_TRANSITION_TYPED,
                                          /*extra_headers=*/std::string());
  EXPECT_TRUE(navigation.WaitForRequestStart());
  // ArkWeb: Stop cancels without rescheduling.
  web_contents()->Stop();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadOnUnsupportedNetworkErrors) {
  EnableAutoReload();
  // ArkWeb: each block verifies a filtered error code does not schedule reload.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_UNKNOWN_URL_SCHEME);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
  // ArkWeb: client cert errors are filtered.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_BAD_SSL_CLIENT_AUTH_CERT);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
  // ArkWeb: certificate errors are filtered.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CERT_INVALID);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
  // ArkWeb: SSL protocol errors are filtered.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_SSL_PROTOCOL_ERROR);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
  // ArkWeb: client-blocked errors are filtered.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_BLOCKED_BY_CLIENT);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
  // ArkWeb: administrator-blocked errors are filtered.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_BLOCKED_BY_ADMINISTRATOR);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
  // ArkWeb: invalid auth credentials are filtered.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_INVALID_AUTH_CREDENTIALS);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadWithoutHttpOrHttps) {
  EnableAutoReload();
  // ArkWeb: data URLs do not activate auto-reload.
  {
    const GURL kTestDataUrl{"data://whatever"};
    NetErrorUrlInterceptor interceptor(kTestDataUrl, net::ERR_ACCESS_DENIED);
    EXPECT_FALSE(NavigateMainFrame(kTestDataUrl));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }

  // ArkWeb: file URLs do not activate auto-reload.
  {
    const GURL kTestFileUrl{"file://whatever"};
    NetErrorUrlInterceptor interceptor(kTestFileUrl, net::ERR_ACCESS_DENIED);
    EXPECT_FALSE(NavigateMainFrame(kTestFileUrl));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NavigationCancelsAutoReload) {
  EnableAutoReload();
  // ArkWeb: force an error to initiate auto-reload.
  auto interceptor = std::make_unique<NetErrorUrlInterceptor>(
      GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
  interceptor.reset();

  // ArkWeb: a new navigation defers; pending reload is cancelled.
  DeferNextNavigationThrottleInserter deferrer(web_contents());
  web_contents()->GetController().LoadURL(GetTestUrl(), content::Referrer(),
                                          ui::PAGE_TRANSITION_TYPED,
                                          /*extra_headers=*/std::string());
  deferrer.WaitForNextNavigationToBeDeferred();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());

  // ArkWeb: cancelling the deferred navigation reschedules the reload.
  deferrer.CancelAndWaitForNavigationToFinish();
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadWhileOffline) {
  EnableAutoReload();
  SimulateNetworkGoingOffline();

  // ArkWeb: error page while offline does not schedule a reload.
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       AutoReloadWhenBrowserComesOnline) {
  EnableAutoReload();
  SimulateNetworkGoingOffline();

  // ArkWeb: while offline, no reload is scheduled.
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());

  // ArkWeb: coming online schedules the reload.
  SimulateNetworkGoingOnline();
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadOnNonErrorPageWhenBrowserComesOnline) {
  EnableAutoReload();
  EXPECT_TRUE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());

  // ArkWeb: offline on a non-error page does not schedule.
  SimulateNetworkGoingOffline();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());

  // ArkWeb: coming online on a non-error page does not schedule.
  SimulateNetworkGoingOnline();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadWhenContentsHidden) {
  EnableAutoReload();
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // ArkWeb: hiding the contents cancels the scheduled reload.
  web_contents()->WasHidden();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       AutoReloadWhenContentsBecomeVisible) {
  EnableAutoReload();
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // ArkWeb: hide cancels, show reschedules.
  web_contents()->WasHidden();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());

  web_contents()->WasShown();
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadOnNonErrorPageWhenContentsBecomeVisible) {
  EnableAutoReload();
  EXPECT_TRUE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());

  // ArkWeb: visibility changes on a non-error page do not schedule.
  web_contents()->WasHidden();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());

  web_contents()->WasShown();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       AutoReloadPreserveSandbox) {
  EnableAutoReload();
  const GURL main_url = embedded_test_server()->GetURL("/title1.html");
  const GURL popup_url = GetTestUrl();
  EXPECT_TRUE(NavigateToURL(shell(), main_url));

  // ArkWeb: create a sandboxed iframe.
  content::RenderFrameHost* opener_top = web_contents()->GetPrimaryMainFrame();
  EXPECT_TRUE(ExecJs(opener_top, R"(
    const iframe = document.createElement("iframe");
    iframe.sandbox = "allow-popups allow-scripts";
    iframe.src = location.href;
    document.body.appendChild(iframe);
  )"));
  EXPECT_TRUE(WaitForLoadStop(web_contents()));
  content::RenderFrameHost* opener_child = GetChild(*opener_top);
  ASSERT_TRUE(opener_child);
  // ArkWeb: verify sandbox flags on the opener iframe.
  using WebSandboxFlags = network::mojom::WebSandboxFlags;
  EXPECT_TRUE(opener_child->IsSandboxed(WebSandboxFlags::kOrigin));
  EXPECT_TRUE(opener_child->IsSandboxed(WebSandboxFlags::kDownloads));
  EXPECT_FALSE(opener_child->IsSandboxed(WebSandboxFlags::kScripts));
  EXPECT_FALSE(opener_child->IsSandboxed(WebSandboxFlags::kPopups));
  EXPECT_EQ("null", EvalJs(opener_child, "window.origin"));

  // ArkWeb: upstream uses ShellAddedObserver (removed in this baseline); use
  // WebContentsAddedObserver instead.
  content::WebContents* popup = nullptr;
  auto interceptor = std::make_unique<NetErrorUrlInterceptor>(
      popup_url, net::ERR_CONNECTION_RESET);
  {
    // ArkWeb: open popup from sandboxed iframe while offline.
    content::WebContentsAddedObserver web_contents_observer;
    EXPECT_TRUE(
        ExecJs(opener_child, content::JsReplace("window.open($1)", popup_url)));
    popup = web_contents_observer.GetWebContents();
    SimulateNetworkGoingOffline(popup);
    EXPECT_FALSE(WaitForLoadStop(popup));
    content::RenderFrameHost* popup_rfh = popup->GetPrimaryMainFrame();
    EXPECT_TRUE(popup_rfh->IsErrorDocument());
  }

  {
    // ArkWeb: go online and reload the popup.
    content::TestNavigationManager navigation(popup, popup_url);
    interceptor.reset();
    SimulateNetworkGoingOnline(popup);
    ForceScheduledAutoReloadNow(popup);
    ASSERT_TRUE(navigation.WaitForNavigationFinished());
    EXPECT_TRUE(navigation.was_successful());
    EXPECT_TRUE(navigation.was_committed());
    content::RenderFrameHost* popup_rfh = popup->GetPrimaryMainFrame();
    EXPECT_FALSE(popup_rfh->IsErrorDocument());

    // ArkWeb: sandbox flags must be preserved after reload.
    EXPECT_TRUE(popup_rfh->IsSandboxed(WebSandboxFlags::kOrigin));
    EXPECT_TRUE(popup_rfh->IsSandboxed(WebSandboxFlags::kDownloads));
    EXPECT_FALSE(popup_rfh->IsSandboxed(WebSandboxFlags::kScripts));
    EXPECT_FALSE(popup_rfh->IsSandboxed(WebSandboxFlags::kPopups));
    EXPECT_EQ("null", EvalJs(popup, "window.origin"));
  }
}

// ArkWeb: NWeb switch gating cases (3).

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadWhenDisabledByDefault) {
  // ArkWeb: switch defaults to off; no reload without EnableAutoReload().
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       AutoReloadWhenEnabledViaSwitch) {
  // ArkWeb: disabled by default, no reload.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }

  // ArkWeb: enabling the switch schedules reload on a new error.
  EnableAutoReload();
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       AutoReloadDisabledAtRuntimeCancelsPending) {
  EnableAutoReload();
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // ArkWeb: disabling cancels the pending timer.
  DisableAutoReload();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());

  // ArkWeb: re-enabling alone does not reschedule; a new event is required.
  EnableAutoReload();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

// ArkWeb: fallback proxy isolation cases (5). The fallback proxy path is gated
// by --enable-nweb-ex plus NeedsReloadWithFallbackProxy() and is not
// constructible in this binary; these cases verify the isolation guarantees.

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       GenericReloadWorksWithoutNwebExSwitch) {
  // ArkWeb: generic reload must work without --enable-nweb-ex.
  ASSERT_FALSE(base::CommandLine::ForCurrentProcess()->HasSwitch(
      "enable-nweb-ex"));
  EnableAutoReload();
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       GenericReloadUsesBackoffNotFixedSingleRetry) {
  EnableAutoReload();
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(base::Seconds(1), GetCurrentAutoReloadDelay());

  // ArkWeb: second delay is 5s (backoff), not a fixed single 1s retry.
  content::TestNavigationManager navigation(web_contents(), GetTestUrl());
  ForceScheduledAutoReloadNow();
  ASSERT_TRUE(navigation.WaitForNavigationFinished());
  EXPECT_FALSE(navigation.was_committed());
  EXPECT_EQ(base::Seconds(5), GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       SwitchToggleDoesNotClearErrorPageState) {
  EnableAutoReload();
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // ArkWeb: disable then re-enable via visibility resume.
  DisableAutoReload();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  EnableAutoReload();
  // ArkWeb: visibility toggle reschedules with the same backoff position.
  web_contents()->WasHidden();
  web_contents()->WasShown();
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       DuplicateErrorPageSuppressionAppliesToGenericReload) {
  EnableAutoReload();
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // ArkWeb: same URL + same error suppresses the duplicate error page.
  content::TestNavigationManager navigation(web_contents(), GetTestUrl());
  ForceScheduledAutoReloadNow();
  ASSERT_TRUE(navigation.WaitForNavigationFinished());
  EXPECT_FALSE(navigation.was_committed());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       StopBehaviorUnaffectedBySwitch) {
  EnableAutoReload();
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());

  // ArkWeb: Stop cancels the timer regardless of switch state.
  web_contents()->Stop();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());

  // ArkWeb: toggling the switch must not resurrect state after Stop.
  DisableAutoReload();
  EnableAutoReload();
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());

  // ArkWeb: a new error page restarts from the first delay.
  NetErrorUrlInterceptor second_interceptor(GetTestUrl(),
                                            net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(GetDelayForReloadCount(0), GetCurrentAutoReloadDelay());
}

// ArkWeb: backoff cap case (1). Deviation from upstream: at most 7 attempts.

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       MaxReloadAttemptsStopsScheduling) {
  EnableAutoReload();
  NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CONNECTION_RESET);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));

  // ArkWeb: force and fail 7 reload attempts (1s, 5s, 30s, 1m, 5m, 10m, 30m).
  for (size_t i = 0; i < 7; ++i) {
    ASSERT_EQ(GetDelayForReloadCount(i), GetCurrentAutoReloadDelay());
    content::TestNavigationManager navigation(web_contents(), GetTestUrl());
    ForceScheduledAutoReloadNow();
    ASSERT_TRUE(navigation.WaitForNavigationFinished());
    EXPECT_FALSE(navigation.was_committed());
  }

  // ArkWeb: the 8th reload is not scheduled (cumulative max 2796s).
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

// ArkWeb: error code filtering cases (8), one per excluded error category.

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadOnCertificateError) {
  EnableAutoReload();
  // ArkWeb: certificate errors are filtered from auto-reload.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_CERT_INVALID);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
  // ArkWeb: another certificate error variant.
  {
    NetErrorUrlInterceptor interceptor(
        GetTestUrl(), net::ERR_CERT_COMMON_NAME_INVALID);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadOnClientCertificateError) {
  EnableAutoReload();
  // ArkWeb: client certificate errors are filtered.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_BAD_SSL_CLIENT_AUTH_CERT);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
  // ArkWeb: signature failure variant.
  {
    NetErrorUrlInterceptor interceptor(
        GetTestUrl(), net::ERR_SSL_CLIENT_AUTH_SIGNATURE_FAILED);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadOnSslProtocolError) {
  EnableAutoReload();
  // ArkWeb: SSL protocol error is filtered.
  NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                     net::ERR_SSL_PROTOCOL_ERROR);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadOnBlockedByClientError) {
  EnableAutoReload();
  // ArkWeb: client-blocked errors are filtered.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                       net::ERR_BLOCKED_BY_CLIENT);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
  // ArkWeb: administrator-blocked errors are filtered.
  {
    NetErrorUrlInterceptor interceptor(
        GetTestUrl(), net::ERR_BLOCKED_BY_ADMINISTRATOR);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
  // ArkWeb: CSP-blocked errors are filtered.
  {
    NetErrorUrlInterceptor interceptor(GetTestUrl(), net::ERR_BLOCKED_BY_CSP);
    EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
    EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
  }
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadOnInvalidAuthCredentials) {
  EnableAutoReload();
  // ArkWeb: invalid auth credentials are filtered.
  NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                     net::ERR_INVALID_AUTH_CREDENTIALS);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadOnUnknownUrlScheme) {
  EnableAutoReload();
  // ArkWeb: unknown URL scheme is filtered.
  NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                     net::ERR_UNKNOWN_URL_SCHEME);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadOnHttpResponseCodeFailure) {
  EnableAutoReload();
  // ArkWeb: HTTP response code failure is filtered.
  NetErrorUrlInterceptor interceptor(GetTestUrl(),
                                     net::ERR_HTTP_RESPONSE_CODE_FAILURE);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

IN_PROC_BROWSER_TEST_F(ErrorPageAutoReloaderBrowserTest,
                       NoAutoReloadOnLocalNetworkAccessChecks) {
  EnableAutoReload();
  // ArkWeb: local network access check blocks are filtered.
  NetErrorUrlInterceptor interceptor(
      GetTestUrl(), net::ERR_BLOCKED_BY_LOCAL_NETWORK_ACCESS_CHECKS);
  EXPECT_FALSE(NavigateMainFrame(GetTestUrl()));
  EXPECT_EQ(std::nullopt, GetCurrentAutoReloadDelay());
}

}  // namespace
}  // namespace OHOS::NWeb
