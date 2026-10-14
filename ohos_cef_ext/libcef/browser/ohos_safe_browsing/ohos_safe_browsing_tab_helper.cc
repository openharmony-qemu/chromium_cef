// Copyright (c) 2023 Huawei Device Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/browser/ohos_safe_browsing/ohos_safe_browsing_tab_helper.h"

#include "arkweb/chromium_ext/base/arkweb_report_statistics.h"
#include "base/base_switches.h"
#include "arkweb/chromium_ext/content/public/common/content_switches_ext.h"
#include "base/command_line.h"
#include "base/json/json_reader.h"
#include "base/json/json_string_value_serializer.h"
#include "base/json/json_writer.h"
#include "components/user_prefs/user_prefs.h"
#include "content/browser/renderer_host/navigation_request.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "crypto/sha2.h"
#include "libcef/browser/alloy/alloy_browser_host_impl.h"
#include "libcef/browser/browser_context.h"
#include "libcef/browser/ohos_safe_browsing/ohos_sb_prefs.h"
#include "libcef/browser/ohos_safe_browsing/safe_browsing_metrics_collector.h"
#include "net/base/net_errors.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/http/http_status_code.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "url/gurl.h"

namespace ohos_safe_browsing {

namespace {

content::BrowserContext* GetBrowserContext() {
  std::vector<CefBrowserContext*> browser_context_all =
      CefBrowserContext::GetAll();
  if (browser_context_all.size() > 0) {
    return browser_context_all[0]->AsBrowserContext();
  }
  return nullptr;
}

}  // namespace

OHSBThreatType TransformMappingType(const std::string& mapping_type) {
  static const std::map<std::string, OHSBThreatType > kSBThreadType = {
    {"THREAT_ILLEGAL", OHSBThreatType::THREAT_ILLEGAL},
    {"THREAT_FRAUD", OHSBThreatType::THREAT_FRAUD},
    {"THREAT_RISK", OHSBThreatType::THREAT_RISK},
    {"THREAT_WARNING", OHSBThreatType::THREAT_WARNING},
    {"NORMAL", OHSBThreatType::THREAT_NONE},
    {"UNPROCESS", OHSBThreatType::THREAT_UNPROCESSED}
  };
 
  auto it = kSBThreadType.find(mapping_type);
  return it != kSBThreadType.end() ? it->second : OHSBThreatType::THREAT_DEFAULT;
}

SafeBrowsingTabHelper::SafeBrowsingTabHelper(content::WebContents* web_contents,
                                             bool incognito_mode,
                                             CefBrowserHostBase* browser)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<SafeBrowsingTabHelper>(*web_contents),
      metrics_collector_(std::make_unique<SafeBrowsingMetricsCollector>()) {
  content::BrowserContext* browser_context = GetBrowserContext();
  if (!browser_context) {
    return;
  }

  if (!browser) {
    return;
  }
  PrefService* pref_service = user_prefs::UserPrefs::Get(browser_context);
  if (!pref_service) {
    return;
  }

  sb_client_.reset(new SbClient(web_contents, pref_service, incognito_mode));
  browser_ = browser;
}

SafeBrowsingTabHelper::~SafeBrowsingTabHelper() = default;

void SafeBrowsingTabHelper::DoQuerySafeBrowsing(
  content::NavigationHandle* navigation_handle,
  NavigationSourceType source_type) {
  if (!navigation_handle->IsInMainFrame()) {
      return;
  }

  if (navigation_handle->GetNetErrorCode() == net::ERR_BLOCKED_BY_CLIENT) {
      return;
  }

  if (content::NavigationRequest::From(navigation_handle)
          ->is_synchronous_renderer_commit()) {
	    LOG(INFO) << "is_synchronous_renderer_commit from source: "
                << static_cast<int>(source_type);

      return;
  }

  if (!navigation_handle->GetURL().SchemeIsHTTPOrHTTPS()) {
      return;
  }

  QuerySafeBrowsingResults(navigation_handle);
}

void SafeBrowsingTabHelper::DidRedirectNavigation(
    content::NavigationHandle* navigation_handle) {
    DoQuerySafeBrowsing(navigation_handle, NavigationSourceType::kRedirect);
}

void SafeBrowsingTabHelper::DidStartNavigation(
    content::NavigationHandle* navigation_handle) {
    metrics_collector_->Reset();
    metrics_collector_->RecordSbStartTime();

    DoQuerySafeBrowsing(navigation_handle, NavigationSourceType::kStart);
}

void SafeBrowsingTabHelper::QuerySafeBrowsingResults(
    content::NavigationHandle* navigation_handle) {
  if (browser_) {
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableNwebEx)){
      if (browser_->AsArkWebBrowserHostExtImpl()
              ->IsSafeBrowsingDetectionDisabled()) {
        LOG(INFO) << "SafeBrowsingDetection is disabled";
        return;
      }      
    }    

    browser_->AsArkWebBrowserHostExtImpl()->HandleSafeBrowsingDetection(
        navigation_handle->GetURL().spec());
  }
}

void SafeBrowsingTabHelper::OnDetectionResult(
    const SafeBrowsingDetectionResult& safeBrowsingDetectionResult) {
  if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE, base::BindOnce(&SafeBrowsingTabHelper::OnDetectionResult,
                                  weak_ptr_factory_.GetWeakPtr(),
                                  safeBrowsingDetectionResult));
    return;
  }

  LOG(INFO) << "SafeBrowsing SA detection with policy: "
            << safeBrowsingDetectionResult.policy
            << ", code: " << safeBrowsingDetectionResult.code
            << ", type: " << safeBrowsingDetectionResult.mapping_type;

  metrics_collector_->RecordSbEndTime();

  OHSBThreatType threat_type =
      TransformMappingType(safeBrowsingDetectionResult.mapping_type);

  if (safeBrowsingDetectionResult.is_from_af_detect) {
    metrics_collector_->SetAfInfo(safeBrowsingDetectionResult.policy, threat_type,
                                   safeBrowsingDetectionResult.code);
  } else {
    metrics_collector_->SetSbInfo(
        static_cast<int>(threat_type), safeBrowsingDetectionResult.code,
        safeBrowsingDetectionResult.policy);
  }

  if (sb_client_) {
    sb_client_->SetEvilUrlPolicyAndHwCode(
        GURL(safeBrowsingDetectionResult.access_url),
        safeBrowsingDetectionResult.policy, threat_type,
        safeBrowsingDetectionResult.code, GURL(""));
  }
}

void SafeBrowsingTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (!navigation_handle->IsInMainFrame() ||
      !navigation_handle->HasCommitted()) {
    return;
  }

  metrics_collector_->SetHost(std::string(navigation_handle->GetURL().host()));
  metrics_collector_->ReportMetrics();
}

void SafeBrowsingTabHelper::SafeBrowsingResponseShowInterstitial(
    int64_t response_key, const std::string& html) const {
  if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&SafeBrowsingTabHelper::SafeBrowsingResponseShowInterstitial,
                       weak_ptr_factory_.GetWeakPtr(), response_key, html));
    return;
  }
  
  auto it = response_map_.find(response_key);
  if (it == response_map_.end()) {
    LOG(ERROR) << "safe browsing response not exist response_key: " << response_key;
    return;
  }

  it->second->ShowInterstitial(html);
}

void SafeBrowsingTabHelper::SafeBrowsingResponseProceed(int64_t response_key) const {
  if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&SafeBrowsingTabHelper::SafeBrowsingResponseProceed,
                       weak_ptr_factory_.GetWeakPtr(), response_key));
    return;
  }

  auto it = response_map_.find(response_key);
  if (it == response_map_.end()) {
    LOG(ERROR) << "safe browsing response not exist response_key: " << response_key;
    return;
  }

  it->second->Proceed();
}

void SafeBrowsingTabHelper::SafeBrowsingResponseBackToSafety(int64_t response_key) const {
  if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&SafeBrowsingTabHelper::SafeBrowsingResponseBackToSafety,
                       weak_ptr_factory_.GetWeakPtr(), response_key));
    return;
  }

  auto it = response_map_.find(response_key);
  if (it == response_map_.end()) {
    LOG(ERROR) << "safe browsing response not exist response_key: " << response_key;
    return;
  }

  it->second->BackToSafety();
}

void SafeBrowsingTabHelper::SafeBrowsingResponseDelete(int64_t response_key) {
  if (!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI)) {
    content::GetUIThreadTaskRunner({})->PostTask(
        FROM_HERE,
        base::BindOnce(&SafeBrowsingTabHelper::SafeBrowsingResponseDelete,
                       weak_ptr_factory_.GetWeakPtr(), response_key));
    return;
  }

  auto it = response_map_.find(response_key);
  if (it != response_map_.end()) {
    response_map_.erase(it);
  }
}

void SafeBrowsingTabHelper::RegisterCefSafeBrowsingResponse(
    int64_t response_key,
    std::unique_ptr<CefSafeBrowsingResponse> response) {
  response_map_[response_key] = std::move(response);
}

void SafeBrowsingTabHelper::WebContentsDestroyed() {
  response_map_.clear();
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(SafeBrowsingTabHelper);

}  // namespace ohos_safe_browsing
