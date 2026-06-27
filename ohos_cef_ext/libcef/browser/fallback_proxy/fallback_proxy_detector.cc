/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#include "cef/ohos_cef_ext/libcef/browser/fallback_proxy/fallback_proxy_detector.h"

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "base/memory/ptr_util.h"
#include "net/base/url_util.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/navigation_handle.h"
#include "arkweb/chromium_ext/url/ohos/log_utils.h"
#include "arkweb/chromium_ext/base/ohos/nweb_engine_event_logger.h"
#include "arkweb/chromium_ext/base/ohos/nweb_engine_event_logger_code.h"
#include "cef/ohos_cef_ext/libcef/browser/fallback_proxy/fallback_proxy_service.h"

namespace fallback_proxy {

namespace {
constexpr base::TimeDelta kProxyConfigUpdateTimeout = base::Seconds(2);
}

NAVIGATION_HANDLE_USER_DATA_KEY_IMPL(AccelerateProxyUserData);

std::unique_ptr<content::NavigationThrottle> FallbackProxyDetector::Create(
    content::NavigationThrottleRegistry& registry) {
  return base::WrapUnique(new FallbackProxyDetector(registry));
}

FallbackProxyDetector::FallbackProxyDetector(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

FallbackProxyDetector::~FallbackProxyDetector() = default;

content::NavigationThrottle::ThrottleCheckResult FallbackProxyDetector::WillStartRequest() {
  content::NavigationHandle* handle = navigation_handle();
  if (!handle || !IsEligibleForProxySwitch(handle)) {
    return content::NavigationThrottle::PROCEED;
  }
      
  GURL url = handle->GetURL();
  LOG(INFO) << "ActiveProxyAccelerate WillStartRequest - url: "
            << url::LogUtils::ConvertUrlWithMask(url.spec());

  bool should_defer_navigation = true;
  bool is_accelerate_proxy = false;
  FallbackProxyService::GetInstance()->UpdateProxyConfigForURL(
      url, should_defer_navigation, is_accelerate_proxy,
      base::BindOnce(&FallbackProxyDetector::OnProxyConfigUpdated,
                     weak_ptr_factory_.GetWeakPtr()));

  if (!AccelerateProxyUserData::GetForNavigationHandle(*handle)) {
    AccelerateProxyUserData::CreateForNavigationHandle(
      *handle, is_accelerate_proxy);
  }

  if (!should_defer_navigation) {
    return content::NavigationThrottle::PROCEED;
  }

  defer_start_time_ = base::TimeTicks::Now();
  proxy_update_timer_.Start(
      FROM_HERE, kProxyConfigUpdateTimeout,
      base::BindOnce(&FallbackProxyDetector::OnProxyConfigUpdateTimeout,
                     weak_ptr_factory_.GetWeakPtr()));

  deferred_ = true;

  return content::NavigationThrottle::DEFER;
}

bool FallbackProxyDetector::IsEligibleForProxySwitch(
    content::NavigationHandle* handle) {
  if (!handle->IsInMainFrame()) {
    LOG(INFO) << "ActiveProxyAccelerate ineligible for proxy switching (not mainframe)";
    return false;
  }
 
  if (!handle->IsRendererInitiated()) {
    LOG(INFO) << "ActiveProxyAccelerate eligible for proxy switching (initiated by the browser)";
    return true;
  }
 
  if (handle->HasUserGesture()) {
    LOG(INFO) << "ActiveProxyAccelerate eligible for proxy switching (has user gesture)";
    return true;
  }
 
  LOG(INFO) << "ActiveProxyAccelerate ineligible for proxy switching (not user-initiated)";
  return false;
}

const char* FallbackProxyDetector::GetNameForLogging() {
  return "FallbackProxyDetector";
}

void FallbackProxyDetector::OnProxyConfigUpdated() {
  if (!deferred_) {
    return;
  }

  deferred_ = false;
  base::TimeDelta elapsed = base::TimeTicks::Now() - defer_start_time_;
  LOG(INFO) << "ActiveProxyAccelerate OnProxyConfigUpdated - Resume after "
            << elapsed.InMilliseconds() << "ms";

  proxy_update_timer_.Stop();
  ReportDeferTime(elapsed);
  Resume();
}

void FallbackProxyDetector::OnProxyConfigUpdateTimeout() {
  if (!deferred_) {
    return;
  }

  deferred_ = false;
  base::TimeDelta elapsed = base::TimeTicks::Now() - defer_start_time_;
  LOG(WARNING) << "ActiveProxyAccelerate OnProxyConfigUpdateTimeout - Timeout after "
               << elapsed.InMilliseconds() << "ms, resuming anyway";

  ReportDeferTime(elapsed);
  Resume();
}

void FallbackProxyDetector::ReportDeferTime(
    const base::TimeDelta& defer_time) {
  GURL url = navigation_handle()->GetURL();
  std::ostringstream ostr;
  ostr << "deferDuration=" << defer_time.InMilliseconds();
 
  // 运维打点数据上报
  base::ohos::ReportEngineEvent(base::ohos::kModuleNet, url.GetHost(),
                                base::ohos::kProxySwitchResult, ostr.str());
  LOG(INFO) << "event_message: " << ostr.str() << ", resource: "
            << url::LogUtils::ConvertUrl(url.spec(), true);
}

}  // namespace fallback_proxy

