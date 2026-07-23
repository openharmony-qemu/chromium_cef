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

#include "cef/ohos_cef_ext/libcef/renderer/ai_page_element_validator_controller.h"

#if BUILDFLAG(IS_OHOS)

#include <optional>
#include <set>
#include <string_view>
#include <map>
#include <vector>

#include "base/containers/contains.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"

#ifndef INSIDE_BLINK
#define INSIDE_BLINK 1
#endif

#include "third_party/blink/public/common/tokens/tokens.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"

namespace arkweb::ai_page {
namespace {

struct ParsedElementId {
  std::string frame_token;
  std::string document_scope_token;
  int dom_node_id = 0;
};

struct ElementLookupResult {
  std::map<int, blink::WebElement> found_elements;
};

std::string ToJson(base::Value::Dict result) {
  std::string json;
  base::JSONWriter::Write(result, &json);
  return json;
}

std::string BuildStatusJson(bool ok, std::string_view error) {
  base::Value::Dict result;
  result.Set("ok", ok);
  if (!error.empty()) {
    result.Set("error", std::string(error));
  }
  return ToJson(std::move(result));
}

std::string SerializeFrameToken(const blink::FrameToken &token) {
  if (token.Is<blink::LocalFrameToken>()) {
    return token.GetAs<blink::LocalFrameToken>().ToString();
  }
  return token.GetAs<blink::RemoteFrameToken>().ToString();
}

std::string NormalizeFrameToken(std::string_view token) {
  constexpr std::string_view kLocalPrefix = "local:";
  constexpr std::string_view kRemotePrefix = "remote:";
  if (token.substr(0, kLocalPrefix.size()) == kLocalPrefix) {
    return std::string(token.substr(kLocalPrefix.size()));
  }
  if (token.substr(0, kRemotePrefix.size()) == kRemotePrefix) {
    return std::string(token.substr(kRemotePrefix.size()));
  }
  return std::string(token);
}

std::string GetDocumentScopeToken(blink::WebLocalFrame *frame) {
  if (!frame || !frame->GetEmbeddingToken()) {
    return std::string();
  }
  return frame->GetEmbeddingToken()->ToString();
}

std::optional<ParsedElementId> ParseElementId(std::string_view element_id) {
  size_t dom_separator = element_id.rfind('|');
  if (dom_separator == std::string_view::npos ||
      dom_separator + 1 >= element_id.size()) {
    return std::nullopt;
  }
  size_t document_separator = element_id.rfind('|', dom_separator - 1);
  if (document_separator == std::string_view::npos || document_separator == 0 ||
      document_separator + 1 == dom_separator) {
    return std::nullopt;
  }

  int dom_node_id = 0;
  if (!base::StringToInt(std::string(element_id.substr(dom_separator + 1)),
                         &dom_node_id) ||
      dom_node_id <= 0) {
    return std::nullopt;
  }

  ParsedElementId parsed;
  parsed.frame_token = std::string(element_id.substr(0, document_separator));
  parsed.document_scope_token = std::string(element_id.substr(
      document_separator + 1, dom_separator - document_separator - 1));
  parsed.dom_node_id = dom_node_id;
  return parsed;
}

void PushChildrenInReverse(const blink::WebNode &node,
                           std::vector<blink::WebNode> *stack) {
  for (blink::WebNode child = node.LastChild(); !child.IsNull();
       child = child.PreviousSibling()) {
    stack->push_back(child);
  }
}

ElementLookupResult LookupElementsByDomNodeId(
    const blink::WebDocument &document,
    const std::set<int> &wanted_dom_node_ids) {
  ElementLookupResult result;
  std::vector<blink::WebNode> stack;
  blink::WebElement root = document.DocumentElement();
  if (!root.IsNull()) {
    stack.push_back(root);
  }

  while (!stack.empty() &&
         result.found_elements.size() < wanted_dom_node_ids.size()) {
    blink::WebNode node = stack.back();
    stack.pop_back();
    if (!node.IsElementNode()) {
      PushChildrenInReverse(node, &stack);
      continue;
    }

    blink::WebElement element = node.To<blink::WebElement>();
    int dom_node_id = node.GetDomNodeId();
    if (base::Contains(wanted_dom_node_ids, dom_node_id) &&
        !base::Contains(result.found_elements, dom_node_id)) {
      result.found_elements.emplace(dom_node_id, element);
    }

    PushChildrenInReverse(node, &stack);
    blink::WebNode shadow_root = element.OpenOrClosedShadowRoot();
    if (!shadow_root.IsNull()) {
      stack.push_back(shadow_root);
    }
  }
  return result;
}

} // namespace

std::string ValidateElementIdResult(blink::WebLocalFrame *frame,
                                    const std::string &element_id) {
  if (!frame) {
    return BuildStatusJson(false, "no_frame");
  }

  std::string current_frame_token =
      NormalizeFrameToken(SerializeFrameToken(frame->GetFrameToken()));
  std::string current_document_scope_token = GetDocumentScopeToken(frame);

  std::optional<ParsedElementId> parsed = ParseElementId(element_id);
  if (!parsed) {
    return BuildStatusJson(false, "invalid nodeid format");
  }

  if (NormalizeFrameToken(parsed->frame_token) != current_frame_token) {
    return BuildStatusJson(false, "frameToken not match");
  }

  if (parsed->document_scope_token != current_document_scope_token) {
    return BuildStatusJson(false, "documentScope not match");
  }

  std::set<int> wanted_dom_node_ids = {parsed->dom_node_id};
  ElementLookupResult lookup =
      LookupElementsByDomNodeId(frame->GetDocument(), wanted_dom_node_ids);

  auto found_it = lookup.found_elements.find(parsed->dom_node_id);
  if (found_it == lookup.found_elements.end()) {
    return BuildStatusJson(false, "domNodeId not found");
  }

  return BuildStatusJson(true, "");
}

} // namespace arkweb::ai_page

#endif // BUILDFLAG(IS_OHOS)
