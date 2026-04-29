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

#ifndef CEF_LIBCEF_BROWSER_SAFE_BROWSING_SAFE_BROWSING_METRICS_COLLECTOR_H_
#define CEF_LIBCEF_BROWSER_SAFE_BROWSING_SAFE_BROWSING_METRICS_COLLECTOR_H_

#include <string>

#include "base/time/time.h"
#include "libcef/browser/ohos_safe_browsing/ohos_sb_threat_type.h"

namespace ohos_safe_browsing {

enum class UserAction {
  kDefault = -1,
  kForbidden = 0,
  kAllow = 1
};

class SafeBrowsingMetricsCollector {
 public:
  SafeBrowsingMetricsCollector();
  ~SafeBrowsingMetricsCollector();

  void Reset();
  void RecordSbStartTime();
  void RecordSbEndTime();
  void RecordAfStartTime();
  void SetHost(const std::string& host);
  void SetSbInfo(int sb_type, int sb_hw_code, int sb_policy);
  void SetAfInfo(int af_policy, OHSBThreatType af_type, int af_hw_code);
  void SetUserAction(UserAction action);
  void ReportMetrics();

 private:
  std::string host_;
  int sb_type_{0};
  int sb_hw_code_{0};
  int sb_policy_{0};
  int sb_user_action_{-1};
  int anti_fraud_check_{0};
  int af_policy_{-1};
  OHSBThreatType af_type_{OHSBThreatType::THREAT_DEFAULT};
  int af_hw_code_{-1};

  base::TimeTicks sb_start_time_;
  base::TimeTicks sb_end_time_;
  base::TimeTicks af_start_time_;
  bool has_sb_start_time_{false};
  bool has_sb_end_time_{false};
  bool has_af_start_time_{false};
};

}  // namespace ohos_safe_browsing

#endif  // CEF_LIBCEF_BROWSER_SAFE_BROWSING_SAFE_BROWSING_METRICS_COLLECTOR_H_