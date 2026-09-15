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

#ifndef CEF_LIBCEF_BROWSER_OHOS_SAFE_BROWSING_OHOS_SAFE_BROWSING_NAVIGATION_THROTTLE_H_
#define CEF_LIBCEF_BROWSER_OHOS_SAFE_BROWSING_OHOS_SAFE_BROWSING_NAVIGATION_THROTTLE_H_

#include "arkweb/chromium_ext/components/safe_browsing/content/browser/ohos_sb_blocking_page_info.h"
#include "base/memory/raw_ptr.h"
#include "content/public/browser/navigation_throttle.h"

namespace ohos_safe_browsing {
class OhosSafeBrowsingNavigationThrottle : public content::NavigationThrottle {
 public:
  static void MaybeCreateAndAdd(
      content::NavigationThrottleRegistry& registry);
  ~OhosSafeBrowsingNavigationThrottle() override = default;
  const char* GetNameForLogging() override;

  content::NavigationThrottle::ThrottleCheckResult WillStartRequest() override;
  content::NavigationThrottle::ThrottleCheckResult WillFailRequest() override;

 private:
  explicit OhosSafeBrowsingNavigationThrottle(content::NavigationThrottleRegistry& registry);

  void CheckEvilUrlAndMaybeRetry();

  void NeedsRetryEvaluateAndDisplayBlockPage();

  base::TimeTicks load_start_time_;
  bool check_evil_url_completed_{false};
  bool needs_show_blocking_page_ {false};
  std::string error_page_content_;
  bool is_defer_{false};
  base::WeakPtrFactory<OhosSafeBrowsingNavigationThrottle> weak_ptr_factory_{this};
};

}  // namespace ohos_safe_browsing

#endif  // CEF_LIBCEF_BROWSER_OHOS_SAFE_BROWSING_OHOS_SAFE_BROWSING_NAVIGATION_THROTTLE_H_
