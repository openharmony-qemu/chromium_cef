/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "cef/ohos_cef_ext/libcef/browser/net/extra_headers_url_throttle.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/http/http_request_headers.h"
#include "services/network/public/cpp/resource_request.h"
#include "url/gurl.h"

namespace throttle {

ExtraHeadersUrlThrottle::ExtraHeadersUrlThrottle(
    const std::string& extra_headers)
    : extra_headers_(extra_headers) {}

ExtraHeadersUrlThrottle::~ExtraHeadersUrlThrottle() = default;

void ExtraHeadersUrlThrottle::WillStartRequest(
    network::ResourceRequest* request,
    bool* defer) {
  // request->headers has all headers include added header.
  AddExtraHeadersIfNeeded(request->url, &request->headers);
  if (!added_headers_.empty()) {
    original_origin_ = url::Origin::Create(request->url);
  }
}

void ExtraHeadersUrlThrottle::WillRedirectRequest(
    net::RedirectInfo* redirect_info,
    const network::mojom::URLResponseHead& response_head,
    bool* defer,
    std::vector<std::string>* to_be_removed_request_headers,
    net::HttpRequestHeaders* modified_request_headers,
    net::HttpRequestHeaders* modified_cors_exempt_request_headers) {

  if (!added_headers_.empty()) {
    bool is_same_domain = net::registry_controlled_domains::SameDomainOrHost(
        redirect_info->new_url, original_origin_,
        net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);

    if (!is_same_domain) {
      // The headers we added must be removed.
      to_be_removed_request_headers->insert(
          to_be_removed_request_headers->end(),
          std::make_move_iterator(added_headers_.begin()),
          std::make_move_iterator(added_headers_.end()));
      added_headers_.clear();
    }
  }
}

void ExtraHeadersUrlThrottle::AddExtraHeadersIfNeeded(
    const GURL& url,
    net::HttpRequestHeaders* headers) {
  if (extra_headers_.empty()) {
    return;
  }

  net::HttpRequestHeaders extra_headers;
  extra_headers.AddHeadersFromString(extra_headers_);
  for (const auto& header : extra_headers.GetHeaderVector()) {
    added_headers_.push_back(header.key);
    if (headers->HasHeader(header.key)) {
      continue;
    }
    headers->SetHeader(header.key, header.value);
  }
}

}  // namespace throttle