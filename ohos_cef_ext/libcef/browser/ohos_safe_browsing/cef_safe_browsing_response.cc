// Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/browser/ohos_safe_browsing/cef_safe_browsing_response.h"

#include <atomic>

#include "components/security_interstitials/content/security_interstitial_tab_helper.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "libcef/browser/ohos_safe_browsing/ohos_safe_browsing_tab_helper.h"
#include "libcef/browser/ohos_safe_browsing/ohos_sb_block_page.h"

namespace ohos_safe_browsing {
namespace {
std::atomic<int64_t> g_next_response_key{0};
}

CefSafeBrowsingResponse::CefSafeBrowsingResponse(
    content::WebContents* web_contents,
    const OhosSbBlockingPageInfo& blocking_page_info,
    base::OnceCallback<void(const std::optional<std::string>&)> show_interstitial_callback)
    : response_key_(g_next_response_key.fetch_add(1, std::memory_order_relaxed)),
      blocking_page_info_(blocking_page_info),
      web_contents_{web_contents},
      show_interstitial_callback_(std::move(show_interstitial_callback)) {
  content::NavigationEntry* visible_entry =
      web_contents_->GetController().GetVisibleEntry();
  if (visible_entry) {
    navigation_entry_unique_id_ = visible_entry->GetUniqueID();
  }
}

void CefSafeBrowsingResponse::ShowInterstitial(const std::string& html) {
  LOG(INFO) << "SafeBrowsingResponse ShowInterstitial";
  if (!web_contents_ || web_contents_->IsBeingDestroyed()) {
    return;
  }

  content::NavigationEntry* visible_entry =
      web_contents_->GetController().GetVisibleEntry();
  if (visible_entry && visible_entry->GetUniqueID() != navigation_entry_unique_id_) {
    LOG(INFO) << "The navigation has changed";
    return;
  }

  if (show_interstitial_called_) {
    LOG(INFO) << "show interstitial has been processed";
    return;
  }

  if (!show_interstitial_callback_) {
    LOG(ERROR) << "show_interstitial_callback_ is nullptr";
    return;
  }

  std::move(show_interstitial_callback_).Run(html);
  show_interstitial_called_ = true;
}

void CefSafeBrowsingResponse::Proceed() {
  LOG(INFO) << "SafeBrowsingResponse Proceed";
  if (!web_contents_ || web_contents_->IsBeingDestroyed()) {
    return;
  }

  if (!show_interstitial_called_) {
    LOG(ERROR) << "SafeBrowsingResponse showInterstitial not called";
    return;
  }

  if (completed_) {
    LOG(ERROR) << "SafeBrowsingResponse response completed";
    return;
  }

  if (blocking_page_info_.info_policy_level == OHSBPolicyLevel::POLICY_LEVEL_BLOCKED) {
    LOG(ERROR) << "The strategy is set to mandatory interception, and the Proceed operation is not allowed";
    return;
  }

  auto* sec_tab_helper =
      security_interstitials::SecurityInterstitialTabHelper::FromWebContents(
          web_contents_);
  if (!sec_tab_helper)
    return;

  content::FrameTreeNodeId frame_tree_node_id =
      web_contents_->GetPrimaryMainFrame()->GetFrameTreeNodeId();
  security_interstitials::SecurityInterstitialPage* block_page =
      sec_tab_helper->GetBlockingPageForFrame(frame_tree_node_id);
  if (!block_page) {
    LOG(ERROR) << "SafeBrowsingResponse current navigation does not have a BlockingPage";
    return;
  }
  block_page->CommandReceived(base::NumberToString(security_interstitials::CMD_PROCEED));
  completed_ = true;
}

void CefSafeBrowsingResponse::BackToSafety() {
  LOG(INFO) << "SafeBrowsingResponse BackToSafety";
  if (!web_contents_ || web_contents_->IsBeingDestroyed()) {
    return;
  }

  if (!show_interstitial_called_) {
    LOG(ERROR) << "SafeBrowsingResponse showInterstitial not called";
    return;
  }

  if (completed_) {
    LOG(ERROR) << "SafeBrowsingResponse response completed";
    return;
  }

  auto* sec_tab_helper =
      security_interstitials::SecurityInterstitialTabHelper::FromWebContents(
          web_contents_);
  if (!sec_tab_helper)
    return;
  content::FrameTreeNodeId frame_tree_node_id =
      web_contents_->GetPrimaryMainFrame()->GetFrameTreeNodeId();
  security_interstitials::SecurityInterstitialPage* block_page =
      sec_tab_helper->GetBlockingPageForFrame(frame_tree_node_id);
  if (!block_page) {
    LOG(ERROR) << "SafeBrowsingResponse current navigation does not have a BlockingPage";
    return;
  }
  block_page->CommandReceived(base::NumberToString(security_interstitials::CMD_DONT_PROCEED));
  completed_ = true;
}
}  // namespace ohos_safe_browsing
