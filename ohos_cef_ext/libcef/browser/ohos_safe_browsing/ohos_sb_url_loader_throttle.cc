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
 
#include "ohos_sb_url_loader_throttle.h"

#include "base/logging.h"
#include "cef/libcef/browser/thread_util.h"
#include "content/public/browser/web_contents.h"
#include "ohos_safe_browsing_tab_helper.h"

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

bool CheckBlockPage(const std::vector<GURL>& server_redirect_chain, content::FrameTreeNodeId frame_tree_node_id) {
  content::WebContents* web_contents =
      content::WebContents::FromFrameTreeNodeId(frame_tree_node_id);
  if (!web_contents || !web_contents->GetBrowserContext()) {
    return false;
  }

  SbClient* sb_client = GetSbClient(web_contents);
  if (!sb_client) {
    LOG(ERROR) << "WillFailRequest has no sb_client.";
    return false;
  }

  return sb_client &&
         sb_client->CheckAndBlockRedirectChain(server_redirect_chain);
}
}

OhosSBURLLoaderThrottle::OhosSBURLLoaderThrottle(content::FrameTreeNodeId frame_tree_node_id) noexcept
    : frame_tree_node_id_(frame_tree_node_id) { }

void OhosSBURLLoaderThrottle::WillStartRequest(network::ResourceRequest* request, bool* defer) {
  if (!request->url.SchemeIsHTTPOrHTTPS()) {
    return;
  }

  server_redirect_chain_.emplace_back(request->url);
  current_url_ = request->url;
  LOG(INFO) << "SafeBrowsing " << __func__ << " timer start";
  timer_.Start(FROM_HERE, base::Seconds(1),
               base::BindRepeating(&OhosSBURLLoaderThrottle::CheckUrl,
                                   weak_factory_.GetWeakPtr()));
}

void OhosSBURLLoaderThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& response_head,
    bool* defer,
    std::vector<std::string>* to_be_removed_request_headers,
    net::HttpRequestHeaders* modified_request_headers,
    net::HttpRequestHeaders* modified_cors_exempt_request_headers) {
  LOG(INFO) << "SafeBrowsing " << __func__
            << " update current url when redirect before url: "
            << url::LogUtils::ConvertUrlWithMask(current_url_.spec())
            << " redirect url: "
            << url::LogUtils::ConvertUrlWithMask(redirect_info->new_url.spec());
  server_redirect_chain_.emplace_back(redirect_info->new_url);
  current_url_ = redirect_info->new_url;
}

void OhosSBURLLoaderThrottle::Stop() {
  stopped_ = true;
  timer_.Stop();
  weak_factory_.InvalidateWeakPtrs();
}

void OhosSBURLLoaderThrottle::CheckUrl() {
  if (stopped_) {
    return;
  }

  content::GetUIThreadTaskRunner({})->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&CheckBlockPage, server_redirect_chain_, frame_tree_node_id_),
      base::BindOnce(&OhosSBURLLoaderThrottle::OnCheckResult, weak_factory_.GetWeakPtr()));
}

void OhosSBURLLoaderThrottle::OnCheckResult(bool should_block) {
  if (stopped_) {
    return;
  }

  if (should_block) {
    LOG(WARNING) << "SafeBrowsing " << __func__
                 << " checked url in block cache and cancel request: "
                 << url::LogUtils::ConvertUrlWithMask(current_url_.spec());
    Stop();
    delegate_->CancelWithError(net::ERR_BLOCKED_BY_CLIENT);
  }
}
}  // namespace ohos_safe_browsing
