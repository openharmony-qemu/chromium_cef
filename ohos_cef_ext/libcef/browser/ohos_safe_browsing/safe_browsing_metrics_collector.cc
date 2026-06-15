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

#include "libcef/browser/ohos_safe_browsing/safe_browsing_metrics_collector.h"

#include "arkweb/chromium_ext/content/public/common/content_switches_ext.h"
#include "base/command_line.h"
#include "arkweb/chromium_ext/base/arkweb_report_statistics.h"
#include "base/json/json_writer.h"
#include "base/logging.h"

namespace ohos_safe_browsing {

namespace {

constexpr char kHost[] = "host";
constexpr char kSbPolicy[] = "sb_policy";
constexpr char kSbType[] = "sb_type";
constexpr char kSbHwCode[] = "sb_hw_code";
constexpr char kSbUserAction[] = "sb_user_action";
constexpr char kAntiFraudCheck[] = "anti_fraud_check";
constexpr char kAntiFraudSaCost[] = "anti_fraud_sa_cost";
constexpr char kSfTotalCost[] = "sf_total_cost";
constexpr char kAfPolicy[] = "af_policy";
constexpr char kAfType[] = "af_type";
constexpr char kAfHwCode[] = "af_hw_code";

}  // namespace

SafeBrowsingMetricsCollector::SafeBrowsingMetricsCollector() = default;

SafeBrowsingMetricsCollector::~SafeBrowsingMetricsCollector() = default;

void SafeBrowsingMetricsCollector::Reset() {
  host_.clear();
  sb_type_ = 0;
  sb_hw_code_ = 0;
  sb_policy_ = 0;
  sb_user_action_ = -1;
  anti_fraud_check_ = 0;
  af_policy_ = -1;
  af_type_ = OHSBThreatType::THREAT_DEFAULT;
  af_hw_code_ = -1;
  has_sb_start_time_ = false;
  has_sb_end_time_ = false;
  has_af_start_time_ = false;
}

void SafeBrowsingMetricsCollector::RecordSbStartTime() {
  sb_start_time_ = base::TimeTicks::Now();
  has_sb_start_time_ = true;
}

void SafeBrowsingMetricsCollector::RecordSbEndTime() {
  sb_end_time_ = base::TimeTicks::Now();
  has_sb_end_time_ = true;
}

void SafeBrowsingMetricsCollector::RecordAfStartTime() {
  af_start_time_ = base::TimeTicks::Now();
  has_af_start_time_ = true;
  anti_fraud_check_ = 1;
}

void SafeBrowsingMetricsCollector::SetHost(const std::string& host) {
  host_ = host;
}

void SafeBrowsingMetricsCollector::SetSbInfo(int sb_type,
                                              int sb_hw_code,
                                              int sb_policy) {
  sb_type_ = sb_type;
  sb_hw_code_ = sb_hw_code;
  sb_policy_ = sb_policy;
}

void SafeBrowsingMetricsCollector::SetAfInfo(int af_policy,
                                             OHSBThreatType af_type,
                                             int af_hw_code) {
  af_policy_ = af_policy;
  af_type_ = af_type;
  af_hw_code_ = af_hw_code;
}

void SafeBrowsingMetricsCollector::SetUserAction(UserAction action) {
  sb_user_action_ = static_cast<int>(action);
}

void SafeBrowsingMetricsCollector::ReportMetrics() {
  if (sb_type_ < OHSBThreatType::THREAT_ILLEGAL ||
      sb_type_ > OHSBThreatType::THREAT_WARNING ||
      !base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kEnableNwebEx)) {
    return;
  }

  base::Value::Dict record;
  record.Set(kHost, host_);
  record.Set(kSbType, sb_type_);
  record.Set(kSbHwCode, sb_hw_code_);
  record.Set(kSbPolicy, sb_policy_);
  record.Set(kSbUserAction, sb_user_action_);
  record.Set(kAntiFraudCheck, anti_fraud_check_);

  if (has_af_start_time_ && has_sb_start_time_) {
    base::TimeDelta af_duration = af_start_time_ - sb_start_time_;
    record.Set(kAntiFraudSaCost, static_cast<int>(af_duration.InMilliseconds()));
  }

  if (has_sb_start_time_ && has_sb_end_time_) {
    base::TimeDelta total_duration = sb_end_time_ - sb_start_time_;
    record.Set(kSfTotalCost, static_cast<int>(total_duration.InMilliseconds()));
  }

  if (af_policy_ >= 0) {
    record.Set(kAfPolicy, af_policy_);
  }

  if (af_type_ != OHSBThreatType::THREAT_DEFAULT) {
    record.Set(kAfType, static_cast<int>(af_type_));
  }

  if (af_hw_code_ >= 0) {
    record.Set(kAfHwCode, af_hw_code_);
  }

  auto json = base::WriteJson(record);
  if (json) {
    base::ohos::OperationStatistics::Statistics(
        base::ohos::REGION_CHINA,
        base::ohos::PLATFORM_OPERATION_ANALYSIS,
        base::ohos::OperationStatistics::GROUP_BECE,
        "safe_browsing_event_common",
        base::ohos::OperationStatistics::DEFAULT_DATA_VERSION,
        json.value(),
        true,
        false,
        true,
        0);
  }
}

}  // namespace ohos_safe_browsing