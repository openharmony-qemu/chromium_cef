// Copyright (c) 2023 Huawei Device Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_SAFE_BROWSING_SAFE_BROWSING_TAB_HELPER_H_
#define CEF_LIBCEF_BROWSER_SAFE_BROWSING_SAFE_BROWSING_TAB_HELPER_H_

#include <map>
#include <memory>

#include "base/atomic_sequence_num.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "include/cef_safe_browsing_detection_callback.h"
#include "libcef/browser/browser_host_base.h"
#include "libcef/browser/ohos_safe_browsing/cef_safe_browsing_response.h"
#include "libcef/browser/ohos_safe_browsing/ohos_sb_client.h"
#include "libcef/browser/ohos_safe_browsing/safe_browsing_metrics_collector.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "services/network/public/cpp/network_connection_tracker.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "base/memory/raw_ptr.h"

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace ohos_safe_browsing {

OHSBThreatType TransformMappingType(const std::string& mapping_type);

class SafeBrowsingTabHelper
    : public CefSafeBrowsingDetectionCallback,
      public content::WebContentsObserver,
      public content::WebContentsUserData<SafeBrowsingTabHelper> {
 public:
  SafeBrowsingTabHelper(const SafeBrowsingTabHelper&) = delete;
  SafeBrowsingTabHelper& operator=(const SafeBrowsingTabHelper&) = delete;
  ~SafeBrowsingTabHelper() override;

  void OnDetectionResult(
      const SafeBrowsingDetectionResult& safeBrowsingDetectionResult) override;

  void RegisterCefSafeBrowsingResponse(
      int64_t response_key,
      std::unique_ptr<CefSafeBrowsingResponse> response);

  void SafeBrowsingResponseShowInterstitial(int64_t response_key, const std::string& html) const;

  void SafeBrowsingResponseProceed(int64_t response_key) const;

  void SafeBrowsingResponseBackToSafety(int64_t response_key) const;

  void SafeBrowsingResponseDelete(int64_t response_key);

  SafeBrowsingMetricsCollector* GetMetricsCollector() {
    return metrics_collector_.get();
  }

  SbClient* sb_client() { return sb_client_.get(); }

IMPLEMENT_REFCOUNTING(SafeBrowsingTabHelper);

  //DidStartNavigation和DidRedirectNavigation的实现只有一行注释差异
  //这里把重复部分提取出来, 用枚举区分调用来源
  enum NavigationSourceType {
    kStart = 0,
    kRedirect = 1
  };
  void DoQuerySafeBrowsing(content::NavigationHandle* navigation_handle,
                           NavigationSourceType source_type);

  // content::WebContentsObserver:
  void DidStartNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidRedirectNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<SafeBrowsingTabHelper>;

  explicit SafeBrowsingTabHelper(content::WebContents* web_contents,
                                 bool incognito_mode,
                                 CefBrowserHostBase* browser);

  void QuerySafeBrowsingResults(content::NavigationHandle* navigation_handle);

  std::unique_ptr<SbClient> sb_client_;
  std::unique_ptr<SafeBrowsingMetricsCollector> metrics_collector_;
  raw_ptr<CefBrowserHostBase> browser_ = nullptr ;

  WEB_CONTENTS_USER_DATA_KEY_DECL();

  // response_key → CefSafeBrowsingResponse
  std::map<int64_t, std::unique_ptr<CefSafeBrowsingResponse>> response_map_;

  base::WeakPtrFactory<SafeBrowsingTabHelper> weak_ptr_factory_{this};
};

}  // namespace ohos_safe_browsing

#endif  // CEF_LIBCEF_BROWSER_SAFE_BROWSING_SAFE_BROWSING_TAB_HELPER_H_
