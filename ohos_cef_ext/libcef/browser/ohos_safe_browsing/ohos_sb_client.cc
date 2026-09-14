// Copyright (c) 2023 Huawei Device Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/browser/ohos_safe_browsing/ohos_sb_client.h"

#include "arkweb/chromium_ext/components/safe_browsing/content/browser/ohos_sb_blocking_page_info.h"
#include "base/containers/lru_cache.h"
#include "base/i18n/rtl.h"
#include "base/logging.h"
#include "base/values.h"
#include "base/base_switches.h"
#include "arkweb/chromium_ext/content/public/common/content_switches_ext.h"
#include "components/security_interstitials/content/security_interstitial_tab_helper.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/common/page_type.h"
#include "content/public/common/referrer.h"
#include "libcef/browser/alloy/alloy_browser_host_impl.h"
#include "libcef/browser/ohos_safe_browsing/ohos_safe_browsing_config_util.h"
#include "libcef/browser/ohos_safe_browsing/ohos_sb_controller_client.h"
#include "libcef/browser/ohos_safe_browsing/ohos_sb_malicious_allowlist.h"
#include "libcef/browser/ohos_safe_browsing/ohos_sb_prefs.h"
#include "libcef/browser/ohos_safe_browsing/ohos_sb_snapshot_info.h"
#if BUILDFLAG(IS_ARKWEB_EXT)
#include "arkweb/ohos_nweb_ex/overrides/cef/libcef/browser/ohos_safe_browsing/safe_browsing_utils.h"
#endif

#if BUILDFLAG(ARKWEB_SAFEBROWSING)
#include "arkweb/chromium_ext/content/browser/web_contents/web_contents_impl_ext.h"
#include "content/browser/web_contents/web_contents_impl.h"
#endif

namespace ohos_safe_browsing {

namespace {
OHSBPolicyLevel GetPolicyLevel(int policy_type) {
  OHSBPolicyType type = static_cast<OHSBPolicyType>(policy_type);
  switch (type) {
    case OHSBPolicyType::POLICY_DANGER_LABEL:
      return OHSBPolicyLevel::POLICY_LEVEL_DANGER_LABEL;
    case OHSBPolicyType::POLICY_HALF_POPUP:
      return OHSBPolicyLevel::POLICY_LEVEL_HALF_POPUP;
    case OHSBPolicyType::POLICY_POPUP_AND_DANGER:
      return OHSBPolicyLevel::POLICY_LEVEL_POPUP_AND_DANGER;
    case OHSBPolicyType::POLICY_CHILD_MODE_PROHIBIT_ACCESS:
    case OHSBPolicyType::POLICY_FORBIDDEN_PROHIBIT_ACCESS:
      return OHSBPolicyLevel::POLICY_LEVEL_BLOCKED;
    default:
      return OHSBPolicyLevel::POLICY_LEVEL_NO_PROMPT;
  }
}
}

SbClient::SbClient(content::WebContents* web_contents,
                   PrefService* prefs,
                   bool incognito_mode)
    : content::WebContentsObserver(web_contents),
      prefs_(prefs),
      incognito_mode_(incognito_mode) {}

SbClient::~SbClient() = default;

// static
bool SbClient::InMaliciousAllowlist(const PrefService* prefs,
                                    const std::string& url,
                                    bool incognito_mode) {
  if (!prefs) {
    return false;
  }

  const base::Value::List& list = prefs->GetList(
      incognito_mode ? kIncognitoMaliciousAllowList : kMaliciousAllowList);

  for (const base::Value& value : list) {
    if (value.GetString() == url) {
      return true;
    }
  }
  return false;
}

void SbClient::BuildAndCacheRedirectChain(const content::LoadCommittedDetails& load_details) {
  NavigationEntrySafeBrowsingData* safe_browsing_data =
      NavigationEntrySafeBrowsingData::GetOrCreate(load_details.entry);
  safe_browsing_data->ClearSbBlockingPageInfoCache();
  if (!load_details.redirect_chain.empty() && load_details.did_replace_entry) {
    safe_browsing_data->SetRedirectChain(load_details.redirect_chain);
    LOG(INFO) << "SafeBrowsing " << __func__ << " set redirect chain size: "
              << load_details.redirect_chain.size() << " entry url: "
              << url::LogUtils::ConvertUrlWithMask(load_details.entry->GetURL().spec());
  }

  safe_browsing_data->AppendRedirectChain(load_details.entry->GetRedirectChain());
  LOG(INFO) << "SafeBrowsing " << __func__ << " append redirect chain size: "
            << load_details.entry->GetRedirectChain().size() << " entry url: "
            << url::LogUtils::ConvertUrlWithMask(load_details.entry->GetURL().spec());

  BlockingPageInfoCache* blocking_page_info_cache =
      BlockingPageInfoCache::FromWebContents(web_contents());
  if (!blocking_page_info_cache) {
    return;
  }
  for (const auto& url : safe_browsing_data->GetRedirectChain()) {
    if (!blocking_page_info_cache->HasBlockingPageInfoForUrl(url)) {
      continue;
    }
 
    OhosSbBlockingPageInfo blocking_page_info = blocking_page_info_cache->GetBlockingPageInfo(url);
    safe_browsing_data->AddSbBlockingPageInfo(blocking_page_info);
  }
}

void SbClient::NavigationEntryCommitted(
    const content::LoadCommittedDetails& load_details) {
  if (!web_contents() || web_contents()->IsBeingDestroyed()) {
    return;
  }

  if (!load_details.is_main_frame) {
    return;
  }

  if (load_details.is_same_document) {
    return;
  }

  if (SafeBrowsingConfigUtil::GetInstance().OptimizeEnable()) {
    NavigationEntrySafeBrowsingData* safe_browsing_data =
      NavigationEntrySafeBrowsingData::GetOrCreate(load_details.entry);
    std::vector<GURL> stored_redirect_chain = safe_browsing_data->GetStoredRedirectChain();
    std::vector<GURL> current_redirect_chain = load_details.entry->GetRedirectChain();

    if (!stored_redirect_chain.empty() &&
        !current_redirect_chain.empty() &&
        stored_redirect_chain.back() != current_redirect_chain.back()) {
      LOG(WARNING) << "SafeBrowsing " << __func__
                   << " change should set block type: "
                   << url::LogUtils::ConvertUrlWithMask(
                          load_details.entry->GetURL().spec());
      safe_browsing_data->SetPreviouslyShownBlockingPageInfo(OhosSbBlockingPageInfo());
    }
    safe_browsing_data->SetStoredRedirectChain(current_redirect_chain);

    BuildAndCacheRedirectChain(load_details);

    OhosSbBlockingPageInfo previously_shown_blocking_page_info =
        safe_browsing_data->GetPreviouslyShownBlockingPageInfo();
    bool is_blocking_page = previously_shown_blocking_page_info.is_valid();

    ui::PageTransition transition = load_details.entry->GetTransitionType();
    bool is_new_navigation = ui::PageTransitionIsNewNavigation(transition);
    LOG(INFO) << "SafeBrowsing " << __func__
              << " is_new_navigation: " << is_new_navigation
              << " is_blocking_page: " << is_blocking_page;
    if (!is_new_navigation && is_blocking_page) {
      const std::vector<GURL>& redirect_chain = safe_browsing_data->GetRedirectChain();
      ReportBlockingPage(load_details.entry, previously_shown_blocking_page_info,
                         redirect_chain.empty() ? GURL() : redirect_chain.front(),
                         redirect_chain.empty() ? GURL() : redirect_chain.back(),
                         redirect_chain.size(),
                         safe_browsing_data->GetIsNewIntercept(), true);
      HandleBlockingPage(previously_shown_blocking_page_info, safe_browsing_data, false);
      return;
    }
  }

  BlockingPageInfoCache* blocking_page_info_cache =
      BlockingPageInfoCache::FromWebContents(web_contents());

  if (!blocking_page_info_cache || blocking_page_info_cache->IsEmpty()) {
    LOG(WARNING) << "The blocking_page_info_cache is empty";
    return;
  }

  if (!blocking_page_info_cache->HasBlockingPageInfoForUrl(load_details.current_commit_entry_url)) {
    LOG(WARNING) << "The blocking_page_info_cache does not contain the result of the current URL.";
    return;
  }

  if (SafeBrowsingConfigUtil::GetInstance().OptimizeEnable()) {
    LOG(INFO) << "SafeBrowsing NavigationEntryCommitted EvaluateAndDisplayBlockPage";
    EvaluateAndDisplayBlockPage();
  } else {
    LOG(INFO) << "SafeBrowsing NavigationEntryCommitted ShowBlockingPage";
    ShowBlockingPage();
  }
  BlockingPageInfoCache::RemoveBlockingPageInfo(web_contents());
}

void SbClient::HandleChildModePolicy(OHSBThreatType block_type) {
  if (block_type == OHSBThreatType::THREAT_WARNING) {
    NotifySafeBrowsingCheckResult(OHSBThreatType::THREAT_RISK);
    NotifySafeBrowsingCheckFinish(OHSBThreatType::THREAT_RISK);
    return;
  }
  NotifySafeBrowsingCheckResult(block_type);
  NotifySafeBrowsingCheckFinish(block_type);
}

void SbClient::ShowBlockingPage() {
  if (!web_contents() || web_contents()->IsBeingDestroyed()) {
    return;
  }

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kEnableNwebEx)){
  if (web_contents()->IsSafeBrowsingDetectionDisabled()) {
    LOG(INFO) << "SafeBrowsingDetection is disabled";
    return;
  }
  }

  content::NavigationEntry* visible_entry =
      web_contents()->GetController().GetVisibleEntry();

  if (!visible_entry) {
    LOG(WARNING) << "SafeBrowsing no visible entry, skip.";
    return;
  }

  GURL virtual_url = visible_entry->GetVirtualURL();
  std::vector<GURL> redirect_chain = visible_entry->GetRedirectChain();
  redirect_chain.push_back(virtual_url);
  std::reverse(redirect_chain.begin(), redirect_chain.end());

  bool is_page_type_normal =
      (visible_entry->GetPageType() == content::PAGE_TYPE_NORMAL);
  if (!is_page_type_normal) {
    LOG(WARNING) << "SafeBrowsing warning: page type is abnormal";
    return;
  }
  std::string lang = base::i18n::GetConfiguredLocale();

  bool is_unprocessed = false;
  bool is_in_allowlist = false;
  int onceprocessed_code = -1;
  int onceprocessed_policy = -1;
  for (auto& url : redirect_chain) {
    BlockingPageInfoCache* blocking_page_info_cache =
        BlockingPageInfoCache::FromWebContents(web_contents());
    if (blocking_page_info_cache && blocking_page_info_cache->HasBlockingPageInfoForUrl(url)) {
      OhosSbBlockingPageInfo blocking_page_info = blocking_page_info_cache->GetBlockingPageInfo(url);
      bool is_display_block_page = false;
      int code = blocking_page_info.info_hw_code;
      int policy = blocking_page_info.info_policy;
      OHSBThreatType block_type = blocking_page_info.info_block_type;

      LOG(INFO) << "SafeBrowsing ShowBlockingPage, hw_code: " << code
                << ", policy: " << policy << ", block_type: " << block_type;

      if (policy == OHSBPolicyType::POLICY_CHILD_MODE_PROHIBIT_ACCESS) {
        is_display_block_page = true;
        HandleChildModePolicy(block_type);
        NotifySafeBrowsingCheckDetail(code, policy, block_type);
      }

      if (block_type == OHSBThreatType::THREAT_WARNING &&
          (policy == OHSBPolicyType::POLICY_DANGER_LABEL ||
           policy == OHSBPolicyType::POLICY_HALF_POPUP)) {
        NotifySafeBrowsingCheckResult(block_type);
        NotifySafeBrowsingCheckFinish(block_type);
        NotifySafeBrowsingCheckDetail(code, policy, block_type);
        return;
      }

      if ((policy == OHSBPolicyType::POLICY_POPUP_AND_DANGER ||
           policy == OHSBPolicyType::POLICY_FORBIDDEN_PROHIBIT_ACCESS) &&
          block_type != OHSBThreatType::THREAT_WARNING) {
        is_display_block_page = true;
        NotifySafeBrowsingCheckResult(block_type);
        NotifySafeBrowsingCheckFinish(block_type);
        NotifySafeBrowsingCheckDetail(code, policy, block_type);
      }

      if (policy != OHSBPolicyType::POLICY_CHILD_MODE_PROHIBIT_ACCESS &&
          MaliciousAllowlist::GetInstance().IsInAllowlist(
              std::string(url.has_host() ? url.host() : url.spec()), incognito_mode_)) {
        LOG(WARNING) << "SafeBrowsing in malicious allowlist.";
        is_in_allowlist = true;        
        break;
      }

      if (is_display_block_page) {
        OverrideAndDisplayBlockingPage(
            blocking_page_info,
            base::BindOnce(&SbClient::DisplayBlockingPage,
                            weak_ptr_factory_.GetWeakPtr(),
                            blocking_page_info));
        return;
      }

      if (block_type == OHSBThreatType::THREAT_UNPROCESSED) {
        is_unprocessed = true;
      }
      onceprocessed_code = code;
      onceprocessed_policy = policy;
    }
  }
  if (is_unprocessed) {
    NotifySafeBrowsingCheckFinish(OHSBThreatType::THREAT_UNPROCESSED);
    NotifySafeBrowsingCheckDetail(onceprocessed_code, onceprocessed_policy,
      OHSBThreatType::THREAT_UNPROCESSED);
  } else {
    NotifySafeBrowsingCheckFinish(OHSBThreatType::THREAT_NONE);
    if(!is_in_allowlist){
      NotifySafeBrowsingCheckDetail(onceprocessed_code, onceprocessed_policy, 
        OHSBThreatType::THREAT_NONE);
    }
  }
}

void SbClient::SaveResultToEntry(const OhosSbBlockingPageInfo& info, content::NavigationEntry* entry) {
  NavigationEntrySafeBrowsingData* safe_browsing_data = NavigationEntrySafeBrowsingData::GetOrCreate(entry);

  if (safe_browsing_data->UrlInRedirectChain(info.info_url)) {
    safe_browsing_data->AddSbBlockingPageInfo(info);
  }
}

void SbClient::SaveResultToAllEntries(const OhosSbBlockingPageInfo& info) {
  int entry_size = web_contents()->GetController().GetEntryCount();
  for (int i = 0; i < entry_size; ++i) {
    content::NavigationEntry* entry =
        web_contents()->GetController().GetEntryAtIndex(i);
    if (!entry) {
      continue;
    }
    SaveResultToEntry(info, entry);
  }
}

void SbClient::EvaluateAndDisplayBlockPage() {
  if (!web_contents() || web_contents()->IsBeingDestroyed()) {
    return;
  }
 
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kEnableNwebEx)) {
    if (web_contents()->IsSafeBrowsingDetectionDisabled()) {
      LOG(INFO) << "SafeBrowsingDetection is disabled";
      return;
    }
  }
 
  content::NavigationEntry* last_committed_entry = web_contents()->GetController().GetLastCommittedEntry();
  if (!last_committed_entry) {
    LOG(WARNING) << "SafeBrowsing last committed entry is nullptr, skip.";
    return;
  }

  bool is_page_type_normal =
      (last_committed_entry->GetPageType() == content::PAGE_TYPE_NORMAL);
  if (!is_page_type_normal) {
    LOG(WARNING) << "SafeBrowsing warning: page type is abnormal";
    return;
  }

  OhosSbBlockingPageInfo blocking_page_info;
  int entry_index = 0;
  int redirect_chain_size = 0;
  GURL first_url;
  GURL last_url;
  bool last_entry_is_blocked_page = false;
 
  for (int current_index = web_contents()->GetController().GetLastCommittedEntryIndex();
       current_index >= 0;
       --current_index) {
    content::NavigationEntry* current_entry = web_contents()->GetController().GetEntryAtIndex(current_index);
    if (!current_entry) {
      break;
    }

    if (current_index == web_contents()->GetController().GetLastCommittedEntryIndex()) {
      const std::vector<GURL>& redirect_chain = current_entry->GetRedirectChain();
      if (redirect_chain.empty()) {
        break;
      }
      last_url = redirect_chain.back();
    }
    redirect_chain_size += 1;
    
    NavigationEntrySafeBrowsingData* current_safe_browsing_data =
        NavigationEntrySafeBrowsingData::GetOrCreate(current_entry);
    OhosSbBlockingPageInfo entry_highest_level_info =
        current_safe_browsing_data->GetHighestThreatInChain();
    LOG(INFO) << "SafeBrowsing EvaluateAndDisplayBlockPage entry_highest_level_info, hw_code: "
              << entry_highest_level_info.info_hw_code
              << ", policy: " << entry_highest_level_info.info_policy
              << ", block_type: " << entry_highest_level_info.info_block_type;

    if (current_index == web_contents()->GetController().GetLastCommittedEntryIndex() &&
        ShouldShowBlockingPage(entry_highest_level_info)) {
      last_entry_is_blocked_page = true;
    }

    if (entry_highest_level_info.info_policy_level > blocking_page_info.info_policy_level) {
      entry_index = current_index;
      blocking_page_info = entry_highest_level_info;
    }

    if (!current_safe_browsing_data->is_js_redirect()) {
      const std::vector<GURL>& redirect_chain = current_entry->GetRedirectChain();
      if (redirect_chain.empty()) {
        break;
      }
      first_url = redirect_chain.front();
      break;
    }
  }

  NavigationEntrySafeBrowsingData* last_safe_browsing_data =
      NavigationEntrySafeBrowsingData::GetOrCreate(last_committed_entry);
  OhosSbBlockingPageInfo last_blocking_page_info =
      last_safe_browsing_data->GetBlockingPageInfo(last_committed_entry->GetURL());
  bool is_unprocessed = (last_blocking_page_info.info_block_type == OHSBThreatType::THREAT_UNPROCESSED);

  if (ShouldShowBlockingPage(blocking_page_info)) {
    bool is_new_intercept =
        ((entry_index == web_contents()->GetController().GetLastCommittedEntryIndex()) && last_entry_is_blocked_page);
    last_safe_browsing_data->SetPreviouslyShownBlockingPageInfo(blocking_page_info);
    last_safe_browsing_data->SetIsNewIntercept(is_new_intercept);
    content::NavigationEntry* entry_to_report =
        web_contents()->GetController().GetEntryAtIndex(entry_index);
    ReportBlockingPage(entry_to_report, blocking_page_info,
                       first_url, last_url, redirect_chain_size,
                       is_new_intercept, false);
  }
  HandleBlockingPage(blocking_page_info, last_safe_browsing_data, is_unprocessed);
}
 
void SbClient::HandleBlockingPage(
    OhosSbBlockingPageInfo& info, NavigationEntrySafeBrowsingData* safe_browsing_data, bool is_unprocessed) {
  LOG(INFO) << "SafeBrowsing HandleBlockingPage VerifiedBlickingPageInfo"
            << " policy: " << info.info_policy
            << " block_type: " << info.info_block_type;

  if (info.info_policy == OHSBPolicyType::POLICY_CHILD_MODE_PROHIBIT_ACCESS &&
      info.info_block_type == OHSBThreatType::THREAT_WARNING) {
    info.info_block_type = OHSBThreatType::THREAT_RISK;
  }

  if (info.info_policy_level <= OHSBPolicyLevel::POLICY_LEVEL_NO_PROMPT) {
    info.info_block_type =
        is_unprocessed? OHSBThreatType::THREAT_UNPROCESSED : OHSBThreatType::THREAT_NONE;
    info.info_original_block_type = info.info_block_type;
  }

  bool is_display_block_page = false;
  if (info.info_policy_level >= OHSBPolicyLevel::POLICY_LEVEL_DANGER_LABEL) {
    NotifySafeBrowsingCheckResult(info.info_block_type);

    bool is_in_allowlist = false;
    if (info.info_policy != OHSBPolicyType::POLICY_CHILD_MODE_PROHIBIT_ACCESS &&
            MaliciousAllowlist::GetInstance().IsInAllowlist(
                info.info_url.has_host() ? info.info_url.GetHost() : info.info_url.spec(), incognito_mode_)) {
      LOG(WARNING) << "SafeBrowsing in malicious allowlist.";
      is_in_allowlist = true;
    }
    if (info.info_policy_level >= OHSBPolicyLevel::POLICY_LEVEL_POPUP_AND_DANGER && !is_in_allowlist) {
      is_display_block_page = true;
      safe_browsing_data->SetPreviouslyShownBlockingPageInfo(info);
    }
  }
  NotifySafeBrowsingCheckFinish(info.info_block_type);
  NotifySafeBrowsingCheckDetail(
      info.info_hw_code, info.info_policy, info.info_original_block_type);

  if (is_display_block_page) {
    OverrideAndDisplayBlockingPage(
        info,
        base::BindOnce(&SbClient::DisplayBlockingPage,
                        weak_ptr_factory_.GetWeakPtr(),
                        info));
  }
}

void SbClient::SetEvilUrlPolicyAndHwCode(const GURL& url,
                                         int policy,
                                         OHSBThreatType block_type,
                                         int hw_code,
                                         const GURL& redirect_url) {
  if (!web_contents() || web_contents()->IsBeingDestroyed()) {
    return;
  }
  OhosSbBlockingPageInfo blocking_page_info(url, block_type, hw_code, policy, GetPolicyLevel(policy), redirect_url);
  BlockingPageInfoCache* blocking_page_info_cache =
      BlockingPageInfoCache::GetOrCreateForWebContents(web_contents(), incognito_mode_);
  blocking_page_info_cache->AddBlockingPageInfo(blocking_page_info);

  content::NavigationEntry* pending_entry =
      web_contents()->GetController().GetPendingEntry();

  content::NavigationEntry* visible_entry =
      web_contents()->GetController().GetVisibleEntry();

  if (!visible_entry) {
    LOG(WARNING) << "SafeBrowsing no visible entry, skip.";
    return;
  }

#if BUILDFLAG(ARKWEB_EXT_NAVIGATION)
  // Save safe browsing check detail to WebContents
  content::WebContentsImpl* web_contents_impl =
      static_cast<content::WebContentsImpl*>(web_contents());
  if (web_contents_impl && web_contents_impl->AsWebContentsImplExt()) {
    int threat_type = static_cast<int>(block_type);
    web_contents_impl->AsWebContentsImplExt()->SetSafeBrowsingCheckDetail(
        hw_code, threat_type, url);
  }
#endif

  if (SafeBrowsingConfigUtil::GetInstance().OptimizeEnable()) {
    SaveResultToAllEntries({url, block_type, hw_code, policy, GetPolicyLevel(policy), redirect_url});
  }
  
  if (!pending_entry && !web_contents()->IsWaitingForResponse() && visible_entry->GetURL() == url) {
    if (SafeBrowsingConfigUtil::GetInstance().OptimizeEnable()) {
  	  LOG(INFO) << "SafeBrowsing SetEvilUrlPolicyAndHwCode EvaluateAndDisplayBlockPage";
      EvaluateAndDisplayBlockPage();
    } else {
  	  LOG(INFO) << "SafeBrowsing SetEvilUrlPolicyAndHwCode ShowBlockingPage";
      ShowBlockingPage();
    }
    BlockingPageInfoCache::RemoveBlockingPageInfo(web_contents());
  }
}

bool SbClient::ShouldShowBlockingPage(const OhosSbBlockingPageInfo& info) const {
  if (info.info_policy_level < OHSBPolicyLevel::POLICY_LEVEL_POPUP_AND_DANGER) {
    return false;
  }
 
  if (info.info_policy != OHSBPolicyType::POLICY_CHILD_MODE_PROHIBIT_ACCESS &&
          MaliciousAllowlist::GetInstance().IsInAllowlist(
              info.info_url.has_host() ? info.info_url.GetHost() : info.info_url.spec(), incognito_mode_)) {
    LOG(WARNING) << "SafeBrowsing in malicious allowlist.";
    return false;
  }
 
  return true;
}

bool SbClient::ShouldBlockCurrentUrl(const GURL& url) const {
  BlockingPageInfoCache* blocking_page_info_cache =
      BlockingPageInfoCache::FromWebContents(web_contents());

  if (!blocking_page_info_cache) {
    LOG(WARNING) << "The blocking_page_info_cache is empty";
    return false;
  }

  if (!blocking_page_info_cache->HasBlockingPageInfoForUrl(url)) {
    LOG(WARNING) << "The blocking_page_info_cache does not contain the result of the current URL.";
    return false;
  }

  OhosSbBlockingPageInfo blocking_page_info = blocking_page_info_cache->GetBlockingPageInfo(url);
  return ShouldShowBlockingPage(blocking_page_info);
}
 
bool SbClient::CheckAndBlockRedirectChain(
    const std::vector<GURL>& server_redirect_chain) const {
  // forward traversal check if url is evil url.
  for (const GURL& url : base::Reversed(server_redirect_chain)) {
    if (ShouldBlockCurrentUrl(url)) {
      return true;
    }
  }
 
  return false;
}

void SbClient::NotifySafeBrowsingResult(OhosSbBlockingPageInfo& info, bool is_unprocessed) {
  LOG(INFO) << "SafeBrowsing VerifiedBlockingPageInfo"
            << " policy: " << info.info_policy
            << " block_type: " << info.info_block_type;

  if (info.info_policy == OHSBPolicyType::POLICY_CHILD_MODE_PROHIBIT_ACCESS &&
      info.info_block_type == OHSBThreatType::THREAT_WARNING) {
    info.info_block_type = OHSBThreatType::THREAT_RISK;
  }

  if (info.info_policy_level <= OHSBPolicyLevel::POLICY_LEVEL_NO_PROMPT) {
    info.info_block_type =
        is_unprocessed? OHSBThreatType::THREAT_UNPROCESSED : OHSBThreatType::THREAT_NONE;
    info.info_original_block_type = info.info_block_type;
  }

  if (info.info_policy_level >= OHSBPolicyLevel::POLICY_LEVEL_DANGER_LABEL) {
    NotifySafeBrowsingCheckResult(info.info_block_type);
  }
  NotifySafeBrowsingCheckFinish(info.info_block_type);
  NotifySafeBrowsingCheckDetail(
      info.info_hw_code, info.info_policy, info.info_original_block_type);
}

std::unique_ptr<SbBlockPage> SbClient::CreateSbBlockPage(
    const OhosSbBlockingPageInfo& blocking_page_info, const std::optional<std::string>& html) {
  std::unique_ptr<SbControllerClient> controller = std::make_unique<SbControllerClient>(
      web_contents(), prefs_, blocking_page_info.info_url, base::i18n::GetConfiguredLocale(), incognito_mode_);
  std::unique_ptr<SbBlockPage> block_page = std::make_unique<SbBlockPage>(
      web_contents(), blocking_page_info.info_url, blocking_page_info.info_policy,
      blocking_page_info.info_block_type, std::move(controller));
  if (html) {
    block_page->SetHTMLContents(*html);
  }
  return block_page;
}

void SbClient::OverrideAndDisplayBlockingPage(
    const OhosSbBlockingPageInfo& blocking_page_info,
    base::OnceCallback<void(const std::optional<std::string>&)> show_interstitial_callback) {
  if (!web_contents() || web_contents()->IsBeingDestroyed()) {
    return;
  }
 
  GURL url = blocking_page_info.info_url;
  int policy = blocking_page_info.info_policy;
  OHSBThreatType block_type = blocking_page_info.info_block_type;
  LOG(INFO) << "SafeBrowsing " << __func__ << ", policy: " << policy
            << ", type: " << block_type;
 
  // Check if external SafeBrowsing interstitial mode is enabled
  if (web_contents()->IsSafeBrowsingExternalInterstitialEnabled()) {
    auto* tab_helper =
        SafeBrowsingTabHelper::FromWebContents(web_contents());
    if (!tab_helper) {
      LOG(ERROR) << "safe browsing tab helper not exist";
      return;
    }
    auto response = std::make_unique<CefSafeBrowsingResponse>(
        web_contents(), blocking_page_info, std::move(show_interstitial_callback));
    int64_t response_key = response->response_key();
    tab_helper->RegisterCefSafeBrowsingResponse(response_key, std::move(response));
 
    NotifySafeBrowsingHit(url, blocking_page_info.info_hw_code, block_type, policy, response_key);
    return;
  }
 
  std::move(show_interstitial_callback).Run(std::nullopt);
}

SbClient::BlockingPageInfoCache::BlockingPageInfoCache(content::WebContents* web_contents, bool incognito_mode)
    : content::WebContentsUserData<SbClient::BlockingPageInfoCache>(*web_contents),
      incognito_mode_(incognito_mode) {}

// static
void SbClient::BlockingPageInfoCache::RemoveBlockingPageInfo(
    content::WebContents* web_contents)
{
  if (!web_contents) {
    return;
  }

  web_contents->RemoveUserData(BlockingPageInfoCache::UserDataKey());
}

OhosSbBlockingPageInfo SbClient::BlockingPageInfoCache::GetBlockingPageInfo(const GURL& url) const {
  auto it = ohos_sb_blocking_page_info_cache_.find(url);
  if (it != ohos_sb_blocking_page_info_cache_.end()) {
    return it->second;
  }
  return OhosSbBlockingPageInfo();
}
 
void SbClient::BlockingPageInfoCache::AddBlockingPageInfo(const OhosSbBlockingPageInfo& blocking_page_info) {
  ohos_sb_blocking_page_info_cache_[blocking_page_info.info_url] = blocking_page_info;
}
 
bool SbClient::BlockingPageInfoCache::HasBlockingPageInfoForUrl(const GURL& url) const {
  return ohos_sb_blocking_page_info_cache_.contains(url);
}

void SbClient::DisplayBlockingPage(
    const OhosSbBlockingPageInfo& blocking_page_info, const std::optional<std::string>& html) {
  if (!web_contents() || web_contents()->IsBeingDestroyed()) {
    return;
  }
  LOG(INFO) << "SafeBrowsing " << __func__ << ", policy: " << blocking_page_info.info_policy
            << ", type: " << blocking_page_info.info_block_type;
  if (SafeBrowsingConfigUtil::GetInstance().OptimizeEnable() &&
      web_contents() && !web_contents()->IsBeingDestroyed()) {
    web_contents()->Stop();
  }

  std::unique_ptr<SbBlockPage> blocking_page = CreateSbBlockPage(blocking_page_info, html);
  base::WeakPtr<content::NavigationHandle> error_page_navigation_handle =
      web_contents()->GetController().LoadPostCommitErrorPage(
          web_contents()->GetPrimaryMainFrame(), blocking_page_info.info_url,
          blocking_page->GetHTMLContents());
  if (error_page_navigation_handle.get()) {
    security_interstitials::SecurityInterstitialTabHelper::
        AssociateBlockingPage(error_page_navigation_handle.get(),
                              std::move(blocking_page));
  }
}

bool SbClient::IsBlockPageShowing() {
  security_interstitials::SecurityInterstitialTabHelper*
      security_interstitial_tab_helper = security_interstitials::
          SecurityInterstitialTabHelper::FromWebContents(web_contents());
  return security_interstitial_tab_helper &&
         security_interstitial_tab_helper->IsDisplayingInterstitial();
}

void SbClient::ReportBlockingPage(
    content::NavigationEntry* entry,
    const OhosSbBlockingPageInfo& blocking_page_info,
    const GURL& first_url,
    const GURL& last_url,
    int redirect_chain_size,
    bool is_new_intercept,
    bool is_reload_or_backforward) {
#if BUILDFLAG(IS_ARKWEB_EXT)
  if (!entry) {
    LOG(WARNING) << "SafeBrowsing " << __func__
                 << " entry is nullptr, report js redirect intercept.";
    return;
  }
 
  NavigationEntrySafeBrowsingData* safe_browsing_data =
      NavigationEntrySafeBrowsingData::GetOrCreate(entry);
  std::vector<GURL> redirect_chain = safe_browsing_data->GetRedirectChain();
  int chain_size = redirect_chain_size;
  if (!chain_size) {
    chain_size = redirect_chain.size();
  }
  ReportJSRedirectIntercept(
      blocking_page_info, first_url, last_url, chain_size, is_new_intercept, is_reload_or_backforward);
#endif
}

void SbClient::NotifySafeBrowsingCheckResult(OHSBThreatType threat_type) {
  if (!web_contents()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckResult: web_contents is null";
    return;
  }
  CefRefPtr<AlloyBrowserHostImpl> browser =
      AlloyBrowserHostImpl::GetBrowserForContents(web_contents());
  if (!browser.get()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckResult: browser is null";
    return;
  }
  CefRefPtr<CefClient> client = browser->GetClient();
  if (!client.get()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckResult: client is null";
    return;
  }
  CefRefPtr<CefLoadHandler> load_handler = client->GetLoadHandler();
  if (!load_handler.get()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckResult: load_handler is null";
    return;
  }
  int type = static_cast<int>(threat_type);
  load_handler->AsArkWebLoadHandlerExt()->OnSafeBrowsingCheckResult(type);
}

void SbClient::NotifySafeBrowsingCheckFinish(OHSBThreatType threat_type){
  if (!web_contents()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckFinish: web_contents is null";
    return;
  }
  CefRefPtr<AlloyBrowserHostImpl> browser =
      AlloyBrowserHostImpl::GetBrowserForContents(web_contents());
  if (!browser.get()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckFinish: browser is null";
    return;
  }
  CefRefPtr<CefClient> client = browser->GetClient();
  if (!client.get()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckFinish: client is null";
    return;
  }
  CefRefPtr<CefLoadHandler> load_handler = client->GetLoadHandler();
  if (!load_handler.get()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckFinish: load_handler is null";
    return;
  }

  int type = static_cast<int>(threat_type);
  load_handler->AsArkWebLoadHandlerExt()->OnSafeBrowsingCheckFinish(type);
}

void SbClient::NotifySafeBrowsingCheckDetail(int code,
                                             int policy,
                                             OHSBThreatType threat_type) {
  if (!web_contents()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckDetail: web_contents is null";
    return;
  }

  CefRefPtr<AlloyBrowserHostImpl> browser =
      AlloyBrowserHostImpl::GetBrowserForContents(web_contents());
  if (!browser.get()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckDetail: browser is null";
    return;
  }
  CefRefPtr<CefClient> client = browser->GetClient();
  if (!client.get()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckDetail: client is null";
    return;
  }
  CefRefPtr<CefLoadHandler> load_handler = client->GetLoadHandler();
  if (!load_handler.get()) {
    LOG(WARNING) << "NotifySafeBrowsingCheckDetail: load_handler is null";
    return;
  }
  int threat = static_cast<int>(threat_type);
  if (policy == OHSBPolicyType::POLICY_CHILD_MODE_PROHIBIT_ACCESS &&
      threat_type == OHSBThreatType::THREAT_WARNING) {
    threat = static_cast<int>(OHSBThreatType::THREAT_RISK);
  }

  load_handler->AsArkWebLoadHandlerExt()->OnSafeBrowsingCheckDetail(
      code, policy, threat);
}

void SbClient::NotifySafeBrowsingHit(const GURL& url,
                                     int code,
                                     OHSBThreatType block_type,
                                     int policy,
                                     int64_t response_key) {
  if (!web_contents()) {
    LOG(WARNING) << "NotifySafeBrowsingHit: web_contents is null";
    return;
  }
 
  CefRefPtr<AlloyBrowserHostImpl> browser =
      AlloyBrowserHostImpl::GetBrowserForContents(web_contents());
  if (!browser.get()) {
    LOG(WARNING) << "NotifySafeBrowsingHit: browser is null";
    return;
  }
  CefRefPtr<CefClient> client = browser->GetClient();
  if (!client.get()) {
    LOG(WARNING) << "NotifySafeBrowsingHit: client is null";
    return;
  }
  CefRefPtr<CefLoadHandler> load_handler = client->GetLoadHandler();
  if (!load_handler.get()) {
    LOG(WARNING) << "NotifySafeBrowsingHit: load_handler is null";
    return;
  }
  int threat = static_cast<int>(block_type);
  load_handler->AsArkWebLoadHandlerExt()->OnSafeBrowsingHit(
      url.spec(), code, policy, threat, response_key);
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(SbClient::BlockingPageInfoCache);

}  // namespace ohos_safe_browsing
