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

#include "cef/ohos_cef_ext/libcef/renderer/click_extractor.h"

#include <queue>

#include "base/logging.h"
#include "base/time/time.h"
#include "third_party/blink/public/web/web_element.h"

namespace arkweb {
namespace click_ext {

namespace {
constexpr size_t kXPathMaxDepth = 20;
constexpr size_t kXPathMaxNodes = 50;
constexpr size_t kTextMaxDepth = 5;
constexpr size_t kTextMaxNodes = 15;
constexpr size_t kTextPerNodeMaxChars = 20;
constexpr int64_t kExtractTimeoutMs = 50;
}

ClickExtResult ClickExtExtractor::Extract(blink::WebNode node) {
  return Extract(node, base::Milliseconds(kExtractTimeoutMs));
}

ClickExtResult ClickExtExtractor::Extract(blink::WebNode node, base::TimeDelta timeout) {
  ClickExtResult result;

  if (node.IsNull()) {
    LOG(WARNING) << "[ClickExt] Extract: node is null";
    return result;
  }

  base::TimeTicks start = base::TimeTicks::Now();

  result.xpath = ExtractXPath(node, timeout);
  result.xpath_elapsed_us = (base::TimeTicks::Now() - start).InMicroseconds();

  if (base::TimeTicks::Now() - start >= timeout) {
    LOG(WARNING) << "[ClickExt] Extract timeout before text collection";
    result.success = !result.xpath.empty();
    return result;
  }

  base::TimeDelta remaining = timeout - (base::TimeTicks::Now() - start);
  result.text_content = ExtractTextContent(node, remaining);
  result.text_elapsed_us = (base::TimeTicks::Now() - start).InMicroseconds() - result.xpath_elapsed_us;

  result.success = !result.xpath.empty() || !result.text_content.empty();

  return result;
}

std::string ClickExtExtractor::ExtractXPath(blink::WebNode node, base::TimeDelta timeout) {
  if (node.IsNull()) {
    return "";
  }

  std::vector<std::string> path_parts;
  blink::WebNode current = node;
  size_t depth = 0;
  size_t total_nodes = 0;
  base::TimeTicks start = base::TimeTicks::Now();

  while (!current.IsNull() && depth < kXPathMaxDepth && total_nodes < kXPathMaxNodes) {
    if (CheckTimeout(start, timeout)) {
      LOG(WARNING) << "[ClickExt] ExtractXPath timeout";
      break;
    }

    if (!current.IsElementNode()) {
      current = current.ParentNode();
      continue;
    }

    size_t index = CalculateSiblingIndex(current, total_nodes);
    std::string part = current.To<blink::WebElement>().TagName().Utf8() + "[" + std::to_string(index) + "]";
    path_parts.push_back(part);

    depth++;
    total_nodes++;
    current = current.ParentNode();
  }

  return BuildXPath(path_parts);
}

std::string ClickExtExtractor::ExtractTextContent(blink::WebNode node, base::TimeDelta timeout) {
  if (node.IsNull()) {
    return "";
  }

  std::string text;
  size_t total_nodes = 0;
  size_t max_actual_depth = 0;

  std::queue<std::pair<blink::WebNode, size_t>> node_queue;
  node_queue.push({node, 0});

  base::TimeTicks start = base::TimeTicks::Now();

  while (!node_queue.empty() && total_nodes < kTextMaxNodes && max_actual_depth <= kTextMaxDepth) {
    if (CheckTimeout(start, timeout)) {
      LOG(WARNING) << "[ClickExt] ExtractTextContent timeout";
      break;
    }

    auto [current_node, current_depth] = node_queue.front();
    node_queue.pop();

    max_actual_depth = std::max(max_actual_depth, static_cast<size_t>(current_depth));
    total_nodes++;

    ProcessTextNode(current_node, text);
    EnqueueChildNodes(current_node, current_depth, node_queue, total_nodes);
  }

  return text;
}

bool ClickExtExtractor::CheckTimeout(base::TimeTicks start, base::TimeDelta timeout) {
  return base::TimeTicks::Now() - start >= timeout;
}

size_t ClickExtExtractor::CalculateSiblingIndex(blink::WebNode node, size_t& total_nodes) {
  blink::WebElement element = node.To<blink::WebElement>();  
  size_t index = 1;
  blink::WebNode sibling = element.PreviousSibling();
  while (!sibling.IsNull() && total_nodes < kXPathMaxNodes) {
    total_nodes++;
    if (sibling.IsElementNode() && sibling.To<blink::WebElement>().TagName() == element.TagName()) {
      index++;
    }
    sibling = sibling.PreviousSibling();
  }
  return index;
}

std::string ClickExtExtractor::BuildXPath(const std::vector<std::string>& path_parts) {
  std::string xpath = "/";
  for (auto it = path_parts.rbegin(); it != path_parts.rend(); ++it) {
    xpath += "/" + *it;
  }
  return xpath;
}

void ClickExtExtractor::ProcessTextNode(const blink::WebNode& node, std::string& text) {
  if (!node.IsTextNode()) {
    return;
  }

  blink::WebString node_value = node.NodeValue();
  if (node_value.IsEmpty()) {
    return;
  }

  if (node_value.length() > kTextPerNodeMaxChars) {
    node_value = node_value.Substring(0, kTextPerNodeMaxChars);
  }
  std::string node_text = node_value.Utf8();
  if (!text.empty()) {
    text += ",";
  }
  text += node_text;
}

void ClickExtExtractor::EnqueueChildNodes(const blink::WebNode& node,
                                          size_t current_depth,
                                          std::queue<std::pair<blink::WebNode, size_t>>& queue,
                                          size_t& total_nodes) {
  if (current_depth + 1 > kTextMaxDepth) {
    return;
  }
  blink::WebNode child = node.FirstChild();
  while (!child.IsNull() && total_nodes < kTextMaxNodes) {
    queue.push({child, current_depth + 1});
    child = child.NextSibling();
  }
}

}  // namespace click_ext
}  // namespace arkweb
