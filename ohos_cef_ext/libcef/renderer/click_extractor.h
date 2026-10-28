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

#ifndef CEF_OHOS_CEF_EXT_LIBCEF_RENDERER_CLICK_EXTRACTOR_H_
#define CEF_OHOS_CEF_EXT_LIBCEF_RENDERER_CLICK_EXTRACTOR_H_

#include <queue>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "third_party/blink/public/web/web_node.h"

namespace arkweb {
namespace click_ext {
struct ClickExtResult {
  std::string xpath;
  std::string text_content;
  bool success = false;
  int64_t xpath_elapsed_us = 0;
  int64_t text_elapsed_us = 0;
};

class ClickExtExtractor {
 public:
  static ClickExtResult Extract(blink::WebNode node);
  static ClickExtResult Extract(blink::WebNode node, base::TimeDelta timeout);

 private:
  static bool CheckTimeout(base::TimeTicks start, base::TimeDelta timeout);
  static size_t CalculateSiblingIndex(blink::WebNode node, size_t& total_nodes);
  static std::string BuildXPath(const std::vector<std::string>& path_parts);
  static std::string ExtractXPath(blink::WebNode node, base::TimeDelta timeout);
  static std::string ExtractTextContent(blink::WebNode node, base::TimeDelta timeout);
  static void EnqueueChildNodes(const blink::WebNode& node,
                                size_t current_depth,
                                std::queue<std::pair<blink::WebNode, size_t>>& queue,
                                size_t& total_nodes);
  static void ProcessTextNode(const blink::WebNode& node, std::string& text);
};

}  // namespace click_ext
}  // namespace arkweb

#endif  // CEF_OHOS_CEF_EXT_LIBCEF_RENDERER_CLICK_EXTRACTOR_H_
