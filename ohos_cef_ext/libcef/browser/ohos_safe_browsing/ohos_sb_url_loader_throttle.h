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

#ifndef CEF_LIBCEF_BROWSER_OHOS_SAFE_BROWSING_OHOS_SB_URL_LOADER_THROTTLE_H_
#define CEF_LIBCEF_BROWSER_OHOS_SAFE_BROWSING_OHOS_SB_URL_LOADER_THROTTLE_H_

#include <string>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/frame_tree_node_id.h"
#include "content/public/browser/web_contents.h"
#include "net/base/net_errors.h"
#include "third_party/blink/public/common/loader/url_loader_throttle.h"
#include "url/origin.h"

class GURL;

namespace ohos_safe_browsing {

class OhosSBURLLoaderThrottle : public blink::URLLoaderThrottle {
 public:
  OhosSBURLLoaderThrottle() noexcept = default;
  explicit OhosSBURLLoaderThrottle(content::FrameTreeNodeId frame_tree_node_id) noexcept;
  ~OhosSBURLLoaderThrottle() = default;

  void WillStartRequest(
        network::ResourceRequest* request, bool* defer) override;
  void WillRedirectRequest(
        net::RedirectInfo* redirect_info,
        const network::mojom::URLResponseHead& response_head,
        bool* defer,
        std::vector<std::string>* to_be_removed_request_headers,
        net::HttpRequestHeaders* modified_request_headers,
        net::HttpRequestHeaders* modified_cors_exempt_request_headers) override;

 private:
  void CheckUrl();
  void OnCheckResult(bool should_block);
  void Stop();

  bool stopped_{false};
  content::FrameTreeNodeId frame_tree_node_id_;
  GURL current_url_;
  std::vector<GURL> server_redirect_chain_;
  base::RepeatingTimer timer_;
  base::WeakPtrFactory<OhosSBURLLoaderThrottle> weak_factory_{this};
};

}  // namespace ohos_safe_browsing

#endif  // CEF_LIBCEF_BROWSER_OHOS_SAFE_BROWSING_OHOS_SB_URL_LOADER_THROTTLE_H_
