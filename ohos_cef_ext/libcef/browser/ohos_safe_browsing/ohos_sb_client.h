// Copyright (c) 2023 Huawei Device Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_SAFE_BROWSING_SB_CLIENT_H_
#define CEF_LIBCEF_BROWSER_SAFE_BROWSING_SB_CLIENT_H_

#include <memory>

#include "arkweb/chromium_ext/components/safe_browsing/content/browser/ohos_navigation_entry_safe_browsing_data.h"
#include "arkweb/chromium_ext/components/safe_browsing/content/browser/ohos_sb_threat_type.h"
#include "components/prefs/pref_service.h"
#include "content/browser/renderer_host/navigation_entry_impl.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "libcef/browser/ohos_safe_browsing/ohos_sb_block_page.h"
#include "base/memory/raw_ptr.h"

namespace ohos_safe_browsing {

class SbClient : public content::WebContentsObserver {
 public:
  explicit SbClient(content::WebContents* web_contents,
                    PrefService* prefs,
                    bool incognito_mode);
  ~SbClient() override;

  // DISALLOW_COPY_AND_ASSIGN
  SbClient(const SbClient&) = delete;
  SbClient& operator=(const SbClient&) = delete;

  class BlockingPageInfoCache
      : public content::WebContentsUserData<BlockingPageInfoCache> {
   public:
    ~BlockingPageInfoCache() override = default;

    BlockingPageInfoCache(const BlockingPageInfoCache&) = delete;
    BlockingPageInfoCache& operator=(const BlockingPageInfoCache&) = delete;

    static void RemoveBlockingPageInfo(content::WebContents* web_contents);

    bool IsEmpty() const { return ohos_sb_blocking_page_info_cache_.empty(); }

    OhosSbBlockingPageInfo GetBlockingPageInfo(const GURL& url) const;

    void AddBlockingPageInfo(const OhosSbBlockingPageInfo& blocking_page_info);

    bool HasBlockingPageInfoForUrl(const GURL& url) const;

   private:
    explicit BlockingPageInfoCache(content::WebContents* web_contents, bool incognito_mode);
    friend class content::WebContentsUserData<BlockingPageInfoCache>;

    bool incognito_mode_{false};
    base::flat_map<GURL, OhosSbBlockingPageInfo> ohos_sb_blocking_page_info_cache_{};

    WEB_CONTENTS_USER_DATA_KEY_DECL();
  };

  void HandleChildModePolicy(OHSBThreatType block_type);

  void ShowBlockingPage();

  void EvaluateAndDisplayBlockPage();
 
  void HandleBlockingPage(
      OhosSbBlockingPageInfo& info, NavigationEntrySafeBrowsingData* safe_browsing_data, bool is_unprocessed);

  bool IsBlockPageShowing();

  void SaveResultToEntry(const OhosSbBlockingPageInfo& info, content::NavigationEntry* entry);

  void SaveResultToAllEntries(const OhosSbBlockingPageInfo& info);

  void BuildAndCacheRedirectChain(const content::LoadCommittedDetails& load_details);

  void SetEvilUrlPolicyAndHwCode(const GURL& url,
                                 int policy,
                                 OHSBThreatType block_type,
                                 int hw_code,
                                 const GURL& redirect_url);

  bool ShouldShowBlockingPage(const OhosSbBlockingPageInfo& info) const;

  bool ShouldBlockCurrentUrl(const GURL& url) const;

  bool CheckAndBlockRedirectChain(const std::vector<GURL>& server_redirect_chain) const;

  void NotifySafeBrowsingResult(OhosSbBlockingPageInfo& info, bool is_unprocessed);

  std::unique_ptr<SbBlockPage> CreateSbBlockPage(
      const OhosSbBlockingPageInfo& blocking_page_info, const std::optional<std::string>& html);

  void OverrideAndDisplayBlockingPage(
      const OhosSbBlockingPageInfo& blocking_page_info,
      base::OnceCallback<void(const std::optional<std::string>&)> show_interstitial_callback);

  static bool InMaliciousAllowlist(const PrefService* prefs,
                                   const std::string& url,
                                   bool incognito_mode);

  bool InMaliciousAllowlist(const std::string& url) {
    return SbClient::InMaliciousAllowlist(prefs_, url, incognito_mode_);
  }

 private:
  // WebContentsObserver implementation
  void NavigationEntryCommitted(
      const content::LoadCommittedDetails& load_details) override;

  void DisplayBlockingPage(const OhosSbBlockingPageInfo& blocking_page_info, const std::optional<std::string>& html);

  void ReportBlockingPage(
      content::NavigationEntry* entry,
      const OhosSbBlockingPageInfo& blocking_page_info,
      const GURL& first_url,
      const GURL& last_url,
      int redirect_chain_size,
      bool is_new_intercept,
      bool is_reload_or_backforward);

  void NotifySafeBrowsingCheckResult(OHSBThreatType threat_type);
  
  void NotifySafeBrowsingCheckDetail(int code,
                                     int policy,
                                     OHSBThreatType threat_type);

  void NotifySafeBrowsingCheckFinish(OHSBThreatType threat_type);

  void NotifySafeBrowsingHit(const GURL& url,
                             int code,
                             OHSBThreatType block_type,
                             int policy,
                             int64_t response_key);
 

  raw_ptr<PrefService> prefs_;
  bool incognito_mode_{false};

  base::WeakPtrFactory<SbClient> weak_ptr_factory_{this};
};

}  // namespace ohos_safe_browsing

#endif  // CEF_LIBCEF_BROWSER_SAFE_BROWSING_SB_CLIENT_H_
