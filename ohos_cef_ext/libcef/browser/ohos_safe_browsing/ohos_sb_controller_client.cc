// Copyright (c) 2023 Huawei Device Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/browser/ohos_safe_browsing/ohos_sb_controller_client.h"

#include "components/prefs/scoped_user_pref_update.h"
#include "components/safe_browsing/core/common/safe_browsing_prefs.h"
#include "components/security_interstitials/content/settings_page_helper.h"
#include "components/security_interstitials/core/metrics_helper.h"
#include "content/browser/renderer_host/navigation_controller_impl.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/referrer.h"
#include "libcef/browser/ohos_safe_browsing/ohos_sb_malicious_allowlist.h"
#include "libcef/browser/ohos_safe_browsing/ohos_sb_prefs.h"
#if BUILDFLAG(ARKWEB_SAFEBROWSING)
#include "arkweb/chromium_ext/components/safe_browsing/content/browser/ohos_navigation_entry_safe_browsing_data.h"
#include "libcef/browser/ohos_safe_browsing/ohos_safe_browsing_config_util.h"
#include "libcef/browser/ohos_safe_browsing/ohos_safe_browsing_tab_helper.h"
#include "libcef/browser/ohos_safe_browsing/safe_browsing_metrics_collector.h"
#endif  // BUILDFLAG(ARKWEB_SAFEBROWSING)

constexpr char kDefaultPageUrl[] = "about:blank";

SbControllerClient::SbControllerClient(content::WebContents* web_contents,
                                       PrefService* prefs,
                                       const GURL& url,
                                       const std::string& app_locale,
                                       bool incognito_mode)
    : SecurityInterstitialControllerClient(web_contents,
                                           GetMetricsHelper(url),
                                           prefs,
                                           app_locale,
                                           GURL(kDefaultPageUrl),
                                           /*settings_page_helper=*/nullptr),
      web_contents_{web_contents},
      prefs_(prefs),
      app_locale_{app_locale},
      url_(url),
      incognito_mode_(incognito_mode) {}

SbControllerClient::~SbControllerClient() {}

// static
std::unique_ptr<MetricsHelper> SbControllerClient::GetMetricsHelper(
    const GURL& url) {
  MetricsHelper::ReportDetails settings;
  settings.metric_prefix = "oh.safe_browsing";

  return std::make_unique<MetricsHelper>(url, settings, nullptr);
}

void SbControllerClient::GoBack() {
#if BUILDFLAG(ARKWEB_SAFEBROWSING)
  ohos_safe_browsing::SafeBrowsingMetricsCollector* metrics_collector = GetMetricsCollector();
  if (metrics_collector) {
    metrics_collector->SetUserAction(ohos_safe_browsing::UserAction::kForbidden);
  }
#endif  // BUILDFLAG(ARKWEB_SAFEBROWSING)

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableNwebEx) &&
      !SecurityInterstitialControllerClient::CanGoBack()) {
    LOG(INFO) << "SbControllerClient::GoBack Close";
    web_contents_->Close();
    return;
  }

  SecurityInterstitialControllerClient::GoBackAfterNavigationCommitted();
  return;
}

void SbControllerClient::Proceed() {
#if BUILDFLAG(ARKWEB_SAFEBROWSING)
  ohos_safe_browsing::SafeBrowsingMetricsCollector* metrics_collector = GetMetricsCollector();
  if (metrics_collector) {
    metrics_collector->SetUserAction(ohos_safe_browsing::UserAction::kAllow);
  }
#endif  // BUILDFLAG(ARKWEB_SAFEBROWSING)

  // With committed interstitials the site has already
  // been added to the allowlist, so reload will proceed.
  Reload();
  return;
}

void SbControllerClient::Reload() {
  if (!web_contents_) {
    return;
  }

#if BUILDFLAG(ARKWEB_SAFEBROWSING)
  ohos_safe_browsing::MaliciousAllowlist::GetInstance().AddToAllowlist(
      std::string(url_.has_host() ? url_.host() : url_.spec()), incognito_mode_);
  if (ohos_safe_browsing::SafeBrowsingConfigUtil::GetInstance().OptimizeEnable()) {
    content::NavigationEntry* entry = web_contents_->GetController().GetPostCommitErrorReplacedEntry();
    if (entry) {
      ohos_safe_browsing::NavigationEntrySafeBrowsingData* safe_browsing_data =
          ohos_safe_browsing::NavigationEntrySafeBrowsingData::GetOrCreate(entry);
      safe_browsing_data->SetPreviouslyShownBlockingPageInfo(ohos_safe_browsing::OhosSbBlockingPageInfo());
    } else {
      LOG(WARNING) << "post commit error replaced entry is nullptr";
    }
  }
  web_contents_->GetController().Reload(content::ReloadType::NORMAL, true);
#endif  // BUILDFLAG(ARKWEB_SAFEBROWSING)
}

PrefService* SbControllerClient::GetPrefService() {
  return prefs_;
}

const std::string& SbControllerClient::GetApplicationLocale() const {
  return app_locale_;
}

const std::string SbControllerClient::GetExtendedReportingPrefName() const {
  return std::string();
}

#if BUILDFLAG(ARKWEB_SAFEBROWSING)
ohos_safe_browsing::SafeBrowsingMetricsCollector* SbControllerClient::GetMetricsCollector() {
  if (!web_contents_) {
    return nullptr;
  }
  auto* tab_helper =
      ohos_safe_browsing::SafeBrowsingTabHelper::FromWebContents(
          web_contents_);
  if (!tab_helper) {
    return nullptr;
  }
  return tab_helper->GetMetricsCollector();
}
#endif  // BUILDFLAG(ARKWEB_SAFEBROWSING)