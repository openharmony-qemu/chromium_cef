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
#include "ohos_safe_browsing_navigation_throttle.h"

#include "arkweb/build/features/features.h"
#if BUILDFLAG(IS_ARKWEB_EXT)
#include "arkweb/ohos_nweb_ex/build/features/features.h"
#endif
#include "base/i18n/rtl.h"
#include "base/memory/ptr_util.h"
#include "components/security_interstitials/content/security_interstitial_tab_helper.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "ohos_safe_browsing_config_util.h"
#include "ohos_safe_browsing_tab_helper.h"
#include "ohos_sb_client.h"
#include "ohos_sb_malicious_allowlist.h"
#if BUILDFLAG(IS_ARKWEB_EXT)
#include "arkweb/ohos_nweb_ex/overrides/cef/libcef/browser/ohos_safe_browsing/safe_browsing_utils.h"
#endif

namespace ohos_safe_browsing {
namespace {
SbClient* GetSbClient(content::WebContents* web_contents) {
  ohos_safe_browsing::SafeBrowsingTabHelper* safe_browsing_tab_helper =
      ohos_safe_browsing::SafeBrowsingTabHelper::FromWebContents(web_contents);
  if (!safe_browsing_tab_helper) {
    LOG(ERROR) << "SafeBrowsingTabHelper has no safe_browsing_tab_helper.";
    return nullptr;
  }

  return safe_browsing_tab_helper->sb_client();
}

bool NeedsRetryCheckEvilUrl(int error_code,
                            const base::TimeDelta& malicious_wait_time,
                            const base::TimeTicks& load_start_time) {
  if (error_code == net::ERR_INTERNET_DISCONNECTED) {
    return false;
  }
 
  if ((base::TimeTicks::Now() - load_start_time) < malicious_wait_time) {
    return true;
  }
 
  return false;
}
}

// static
void OhosSafeBrowsingNavigationThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  if (!handle.IsInPrimaryMainFrame() && !handle.IsInPrerenderedMainFrame()) {
    return;
  }

  if (!SafeBrowsingConfigUtil::GetInstance().SbOverlayErrorPage()) {
    return;
  }

  registry.AddThrottle(base::WrapUnique(
      new OhosSafeBrowsingNavigationThrottle(registry)));
}

OhosSafeBrowsingNavigationThrottle::OhosSafeBrowsingNavigationThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

const char* OhosSafeBrowsingNavigationThrottle::GetNameForLogging() {
  return "OhosSafeBrowsingNavigationThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
OhosSafeBrowsingNavigationThrottle::WillStartRequest() {
  load_start_time_ = base::TimeTicks::Now();
  return content::NavigationThrottle::PROCEED;
}

content::NavigationThrottle::ThrottleCheckResult
OhosSafeBrowsingNavigationThrottle::WillFailRequest() {
  content::NavigationHandle* handle = navigation_handle();
  if (!handle->IsInPrimaryMainFrame()) {
    return content::NavigationThrottle::PROCEED;
  }

  CheckEvilUrlAndMaybeRetry();

  if (!check_evil_url_completed_) {
    is_defer_ = true;
    return content::NavigationThrottle::DEFER;
  }

  if (needs_show_blocking_page_) {
    return content::NavigationThrottle::ThrottleCheckResult(
      CANCEL, net::ERR_BLOCKED_BY_CLIENT, error_page_content_);
  }

  return content::NavigationThrottle::PROCEED;
}

void OhosSafeBrowsingNavigationThrottle::CheckEvilUrlAndMaybeRetry() {
  content::NavigationHandle* handle = navigation_handle();
  content::WebContents* web_contents = handle->GetWebContents();
  GURL url = handle->GetURL();
  SbClient::BlockingPageInfoCache* blocking_page_info_cache =
      SbClient::BlockingPageInfoCache::FromWebContents(web_contents);

  if (blocking_page_info_cache && blocking_page_info_cache->HasBlockingPageInfoForUrl(url)) {
    check_evil_url_completed_ = true;
    SbClient* sb_client = GetSbClient(web_contents);
    if (sb_client && sb_client->ShouldBlockCurrentUrl(url)) {
      needs_show_blocking_page_ = true;

      OhosSbBlockingPageInfo blocking_page_info = blocking_page_info_cache->GetBlockingPageInfo(url);
      LOG(INFO) << "SafeBrowsing WillFailRequest blocking_page_info"
                << " policy: " << blocking_page_info.info_policy
                << " block_type: " << blocking_page_info.info_block_type;
#if BUILDFLAG(IS_ARKWEB_EXT)
      ReportSbOverlayErrorPage(
          url, blocking_page_info.info_block_type, blocking_page_info.info_policy,
          blocking_page_info.info_hw_code, handle->GetNetErrorCode());
#endif
          
      sb_client->NotifySafeBrowsingResult(blocking_page_info, false);
      std::unique_ptr<SbBlockPage> blocking_page = sb_client->CreateSbBlockPage(blocking_page_info);
      security_interstitials::SecurityInterstitialTabHelper::AssociateBlockingPage(
          handle, std::move(blocking_page));
      error_page_content_ = blocking_page->GetHTMLContents();
      if (is_defer_) {
        CancelDeferredNavigation(content::NavigationThrottle::ThrottleCheckResult(
            CANCEL, net::ERR_BLOCKED_BY_CLIENT, error_page_content_));
      }
      return;
    }

    if (is_defer_) {
      Resume();
    }
    return;
  }

  int cloud_control_malicious_wait_value =
      SafeBrowsingConfigUtil::GetInstance().GetMaliciousUrlCheckWaitTime();
  base::TimeDelta malicious_wait_time = base::Milliseconds(cloud_control_malicious_wait_value);
  if (NeedsRetryCheckEvilUrl(handle->GetNetErrorCode(), malicious_wait_time, load_start_time_)) {
    content::GetUIThreadTaskRunner({})->PostDelayedTask(
      FROM_HERE, base::BindOnce(&OhosSafeBrowsingNavigationThrottle::CheckEvilUrlAndMaybeRetry,
                                weak_ptr_factory_.GetWeakPtr()),
      (malicious_wait_time - (base::TimeTicks::Now() - load_start_time_)));
  } else {
    check_evil_url_completed_ = true;
    if (is_defer_) {
      Resume();
    }
  }
}

}  // namespace ohos_safe_browsing
