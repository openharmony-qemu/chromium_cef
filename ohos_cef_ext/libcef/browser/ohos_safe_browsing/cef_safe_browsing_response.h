// Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_SAFE_BROWSING_CEF_SAFE_BROWSING_RESPONSE_H_
#define CEF_LIBCEF_BROWSER_SAFE_BROWSING_CEF_SAFE_BROWSING_RESPONSE_H_

#include <string>

#include "arkweb/chromium_ext/components/safe_browsing/content/browser/ohos_sb_blocking_page_info.h"
#include "base/atomic_sequence_num.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "url/gurl.h"

namespace content {
class WebContents;
}  // namespace content

namespace ohos_safe_browsing {

class SafeBrowsingTabHelper;
class SbBlockPage;

class CefSafeBrowsingResponse {
 public:
  CefSafeBrowsingResponse(content::WebContents* web_contents,
                          const OhosSbBlockingPageInfo& blocking_page_info,
                          base::OnceCallback<void(const std::optional<std::string>&)> show_interstitial_callback);

  CefSafeBrowsingResponse(const CefSafeBrowsingResponse&) = delete;
  CefSafeBrowsingResponse& operator=(const CefSafeBrowsingResponse&) = delete;
  CefSafeBrowsingResponse(CefSafeBrowsingResponse&&) = delete;
  CefSafeBrowsingResponse& operator=(CefSafeBrowsingResponse&&) = delete;
  ~CefSafeBrowsingResponse() = default;

  void ShowInterstitial(const std::string& html);

  void Proceed();

  void BackToSafety();

  // ---- Properties ----
  int64_t response_key() const { return response_key_; }

 private:
  int64_t response_key_ = 0;
  int navigation_entry_unique_id_ = 0;
  bool show_interstitial_called_ = false;
  bool completed_ = false;
  OhosSbBlockingPageInfo blocking_page_info_;
  raw_ptr<content::WebContents> web_contents_;
  base::OnceCallback<void(const std::optional<std::string>&)> show_interstitial_callback_;
};

}  // namespace ohos_safe_browsing

#endif  // CEF_LIBCEF_BROWSER_SAFE_BROWSING_CEF_SAFE_BROWSING_RESPONSE_H_
