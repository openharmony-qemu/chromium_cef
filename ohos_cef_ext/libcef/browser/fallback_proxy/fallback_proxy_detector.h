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

#ifndef CEF_LIBCEF_BROWSER_FALLBACK_PROXY_DETECTOR_H_
#define CEF_LIBCEF_BROWSER_FALLBACK_PROXY_DETECTOR_H_

#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle.h"
#include "content/public/browser/navigation_handle_user_data.h"

namespace content {
class NavigationThrottleRegistry;
}

namespace fallback_proxy {

class AccelerateProxyUserData
    : public content::NavigationHandleUserData<AccelerateProxyUserData> {
 public:
  ~AccelerateProxyUserData() override = default;

  bool is_accelerate_proxy() const { return is_accelerate_proxy_; }

 private:
  friend class content::NavigationHandleUserData<AccelerateProxyUserData>;

  explicit AccelerateProxyUserData(content::NavigationHandle& navigation_handle,
                                   bool is_accelerate_proxy)
      : is_accelerate_proxy_(is_accelerate_proxy) {}

  const bool is_accelerate_proxy_;

  NAVIGATION_HANDLE_USER_DATA_KEY_DECL();
};

class FallbackProxyDetector : public content::NavigationThrottle {
 public:
  static std::unique_ptr<content::NavigationThrottle> Create(
      content::NavigationThrottleRegistry& registry);

  explicit FallbackProxyDetector(
      content::NavigationThrottleRegistry& registry);
  ~FallbackProxyDetector() override;

  content::NavigationThrottle::ThrottleCheckResult WillStartRequest() override;
  const char* GetNameForLogging() override;

 private:
  bool IsEligibleForProxySwitch(content::NavigationHandle* handle);
  void OnProxyConfigUpdated();
  void OnProxyConfigUpdateTimeout();
  void ReportDeferTime(const base::TimeDelta& defer_time);

  bool deferred_ = false;
  base::TimeTicks defer_start_time_;
  base::OneShotTimer proxy_update_timer_;
  base::WeakPtrFactory<FallbackProxyDetector> weak_ptr_factory_{this};
};

}  // namespace fallback_proxy

#endif  // CEF_LIBCEF_BROWSER_FALLBACK_PROXY_DETECTOR_H_

