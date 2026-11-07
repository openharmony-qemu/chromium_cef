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

#include "cef/ohos_cef_ext/libcef/renderer/ai_page_annotation_controller.h"

#if BUILDFLAG(IS_OHOS)

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/contains.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"

#ifndef INSIDE_BLINK
#define INSIDE_BLINK 1
#endif

#include "third_party/blink/public/common/tokens/tokens.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_frame.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/node.h"
#include "third_party/blink/renderer/core/geometry/dom_rect.h"
#include "third_party/blink/renderer/core/geometry/dom_rect_list.h"
#include "third_party/blink/renderer/core/html/html_element.h"
#include "third_party/blink/renderer/core/html_names.h"
#include "third_party/blink/renderer/platform/wtf/text/atomic_string.h"
#include "third_party/blink/renderer/platform/wtf/text/wtf_string.h"
#include "ui/gfx/geometry/rect_f.h"

namespace arkweb::ai_page {
namespace {

constexpr char kOverlayRootId[] = "__arkweb_page_annotation_overlay_root__";
constexpr int kDefaultMaxMarks = 350;
constexpr int kMaxAllowedMaxMarks = 1000;
constexpr double kMinVisibleAnnotationArea = 20.0;

struct ParsedElementId {
  std::string frame_token;
  std::string document_scope_token;
  int dom_node_id = 0;
};

struct AnnotationRequest {
  std::string element_id;
  int dom_node_id = 0;
};

struct AnnotationDocumentScanResult {
  std::map<int, blink::WebElement> found_elements;
  base::Value::List child_frames;
};

std::string ToJson(base::Value::Dict result) {
  std::string json;
  base::JSONWriter::Write(result, &json);
  return json;
}

base::Value::List BuildStringList(const std::set<std::string> &strings) {
  base::Value::List list;
  for (const std::string &string : strings) {
    list.Append(string);
  }
  return list;
}

std::optional<base::Value::Dict> ParseParams(const std::string &params) {
  std::optional<base::Value> value =
      base::JSONReader::Read(params, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!value || !value->is_dict()) {
    return std::nullopt;
  }
  return std::move(value->GetDict());
}

std::string BuildStatusJson(bool ok, std::string_view error) {
  base::Value::Dict result;
  result.Set("ok", ok);
  if (!error.empty()) {
    result.Set("error", std::string(error));
  }
  return ToJson(std::move(result));
}

std::string GetStringField(const base::Value::Dict &dict, const char *key) {
  const base::Value *value = dict.Find(key);
  return value && value->is_string() ? value->GetString() : std::string();
}

std::optional<double> ReadNumericField(const base::Value::Dict &source,
                                       const char *key) {
  const base::Value *value = source.Find(key);
  if (!value) {
    return std::nullopt;
  }
  if (value->is_int()) {
    return value->GetInt();
  }
  if (value->is_double()) {
    return value->GetDouble();
  }
  return std::nullopt;
}

bool IsHexDigit(char value) {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
         (value >= 'A' && value <= 'F');
}

bool IsSupportedCssColor(const std::string &value) {
  if (value.size() != 4 && value.size() != 7 && value.size() != 9) {
    return false;
  }
  if (value[0] != '#') {
    return false;
  }
  for (size_t i = 1; i < value.size(); ++i) {
    if (!IsHexDigit(value[i])) {
      return false;
    }
  }
  return true;
}

void CopyColorField(const base::Value::Dict &source, const char *key,
                    base::Value::Dict *target) {
  const std::string *value = source.FindString(key);
  if (value && IsSupportedCssColor(*value)) {
    target->Set(key, *value);
  }
}

void CopyClampedNumberField(const base::Value::Dict &source, const char *key,
                            double min, double max, base::Value::Dict *target) {
  std::optional<double> value = ReadNumericField(source, key);
  if (value) {
    target->Set(key, std::clamp(*value, min, max));
  }
}

void CopyBoolField(const base::Value::Dict &source, const char *key,
                   base::Value::Dict *target) {
  const base::Value *value = source.Find(key);
  if (value && value->is_bool()) {
    target->Set(key, value->GetBool());
  }
}

int ReadMaxMarksFromStyle(const base::Value::Dict &params) {
  const base::Value *style_value = params.Find("style");
  if (!style_value || !style_value->is_dict()) {
    return kDefaultMaxMarks;
  }
  std::optional<double> max_marks =
      ReadNumericField(style_value->GetDict(), "maxMarks");
  if (!max_marks || *max_marks <= 0) {
    return kDefaultMaxMarks;
  }
  if (*max_marks >= kMaxAllowedMaxMarks) {
    return kMaxAllowedMaxMarks;
  }
  return static_cast<int>(*max_marks);
}

base::Value::Dict BuildSanitizedStyle(const base::Value::Dict &params) {
  base::Value::Dict sanitized;
  const base::Value *style_value = params.Find("style");
  if (!style_value || !style_value->is_dict()) {
    return sanitized;
  }

  const base::Value::Dict &style = style_value->GetDict();
  std::optional<double> max_marks = ReadNumericField(style, "maxMarks");
  if (max_marks && *max_marks > 0) {
    sanitized.Set("maxMarks", *max_marks >= kMaxAllowedMaxMarks
                                  ? kMaxAllowedMaxMarks
                                  : static_cast<int>(*max_marks));
  }

  const base::Value *border_value = style.Find("border");
  if (border_value && border_value->is_dict()) {
    base::Value::Dict border;
    const base::Value::Dict &source = border_value->GetDict();
    CopyColorField(source, "color", &border);
    CopyClampedNumberField(source, "width", 1, 8, &border);
    if (!border.empty()) {
      sanitized.Set("border", std::move(border));
    }
  }

  const base::Value *label_value = style.Find("label");
  if (label_value && label_value->is_dict()) {
    base::Value::Dict label;
    const base::Value::Dict &source = label_value->GetDict();
    CopyClampedNumberField(source, "fontSize", 8, 32, &label);
    CopyColorField(source, "fontColor", &label);
    CopyColorField(source, "backgroundColor", &label);
    CopyBoolField(source, "avoidCover", &label);
    if (!label.empty()) {
      sanitized.Set("label", std::move(label));
    }
  }
  return sanitized;
}

std::string SerializeFrameToken(const blink::FrameToken &token) {
  if (token.Is<blink::LocalFrameToken>()) {
    return token.GetAs<blink::LocalFrameToken>().ToString();
  }
  return token.GetAs<blink::RemoteFrameToken>().ToString();
}

std::string NormalizeFrameToken(std::string_view token) {
  static constexpr std::string_view kLocalPrefix = "local:";
  static constexpr std::string_view kRemotePrefix = "remote:";
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

blink::Element *GetCoreElement(const blink::WebElement &element) {
  blink::WebNode node = element;
  return node.Unwrap<blink::Element>();
}

void AddRect(double left, double top, double width, double height,
             base::Value::List *rects) {
  if (width <= 0 || height <= 0) {
    return;
  }
  base::Value::Dict rect;
  rect.Set("left", left);
  rect.Set("top", top);
  rect.Set("right", left + width);
  rect.Set("bottom", top + height);
  rect.Set("width", width);
  rect.Set("height", height);
  rects->Append(std::move(rect));
}

bool IsHitTestVisibleRect(blink::Document *document, blink::Element *element,
                          double left, double top, double width,
                          double height) {
  if (!document || !element || width <= 0 || height <= 0) {
    return false;
  }

  double center_x = left + width / 2;
  double center_y = top + height * 3 / 5;
  blink::Element *hit_element = document->ElementFromPoint(center_x, center_y);
  return hit_element && (hit_element == element ||
                         element->ContainsIncludingHostElements(*hit_element));
}

base::Value::List BuildClientRects(const blink::WebElement &element) {
  base::Value::List output;
  blink::Element *core_element = GetCoreElement(element);
  if (!core_element) {
    return output;
  }

  blink::Document *document = &core_element->GetDocument();
  bool has_positive_client_rect = false;
  blink::DOMRectList *rects = core_element->getClientRects();
  if (rects) {
    for (unsigned i = 0; i < rects->length(); ++i) {
      blink::DOMRect *rect = rects->item(i);
      if (!rect || rect->width() <= 0 || rect->height() <= 0) {
        continue;
      }
      has_positive_client_rect = true;
      if (!IsHitTestVisibleRect(document, core_element, rect->left(),
                                rect->top(), rect->width(), rect->height())) {
        continue;
      }
      AddRect(rect->left(), rect->top(), rect->width(), rect->height(),
              &output);
    }
  }
  if (!output.empty() || has_positive_client_rect) {
    return output;
  }

  gfx::RectF bounds = core_element->GetBoundingClientRectNoLifecycleUpdate();
  if (IsHitTestVisibleRect(document, core_element, bounds.x(), bounds.y(),
                           bounds.width(), bounds.height())) {
    AddRect(bounds.x(), bounds.y(), bounds.width(), bounds.height(), &output);
  }
  return output;
}

std::optional<base::Value::Dict>
BuildFirstClientRect(const blink::WebElement &element) {
  base::Value::List rects = BuildClientRects(element);
  for (const base::Value &rect : rects) {
    if (rect.is_dict()) {
      return rect.GetDict().Clone();
    }
  }
  return std::nullopt;
}

void OffsetRectOrigin(base::Value::Dict *rect, double x_offset,
                      double y_offset) {
  if (!rect || (x_offset == 0 && y_offset == 0)) {
    return;
  }
  std::optional<double> left = ReadNumericField(*rect, "left");
  std::optional<double> top = ReadNumericField(*rect, "top");
  std::optional<double> width = ReadNumericField(*rect, "width");
  std::optional<double> height = ReadNumericField(*rect, "height");
  if (!left || !top || !width || !height) {
    return;
  }
  rect->Set("left", *left + x_offset);
  rect->Set("top", *top + y_offset);
  rect->Set("right", *left + x_offset + *width);
  rect->Set("bottom", *top + y_offset + *height);
}

void PushChildrenInReverse(const blink::WebNode &node,
                           std::vector<blink::WebNode> *stack) {
  for (blink::WebNode child = node.LastChild(); !child.IsNull();
       child = child.PreviousSibling()) {
    stack->push_back(child);
  }
}

AnnotationDocumentScanResult
ScanAnnotationDocument(const blink::WebDocument &document,
                       const std::set<int> &wanted_dom_node_ids,
                       bool collect_child_frames) {
  AnnotationDocumentScanResult result;
  std::vector<blink::WebNode> stack;
  blink::WebElement root = document.DocumentElement();
  if (!root.IsNull()) {
    stack.push_back(root);
  }

  while (!stack.empty() &&
         (collect_child_frames ||
          result.found_elements.size() < wanted_dom_node_ids.size())) {
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

    if (collect_child_frames) {
      if (blink::WebFrame *child_frame =
              blink::WebFrame::FromFrameOwnerElement(element)) {
        std::optional<base::Value::Dict> rect = BuildFirstClientRect(element);
        if (rect) {
          if (blink::Element *core_element = GetCoreElement(element)) {
            OffsetRectOrigin(&*rect, core_element->clientLeft(),
                             core_element->clientTop());
          }
          base::Value::Dict child;
          child.Set("frameToken", NormalizeFrameToken(SerializeFrameToken(
                                      child_frame->GetFrameToken())));
          child.Set("rect", std::move(*rect));
          result.child_frames.Append(std::move(child));
        }
      }
    }

    PushChildrenInReverse(node, &stack);
    blink::WebNode shadow_root = element.OpenOrClosedShadowRoot();
    if (!shadow_root.IsNull()) {
      stack.push_back(shadow_root);
    }
  }
  return result;
}

std::optional<double> ReadNumber(const base::Value::Dict &dict,
                                 const char *key) {
  const base::Value *value = dict.Find(key);
  if (!value) {
    return std::nullopt;
  }
  if (value->is_int()) {
    return value->GetInt();
  }
  if (value->is_double()) {
    return value->GetDouble();
  }
  return std::nullopt;
}

bool AppendSanitizedRect(const base::Value &value, base::Value::List *rects) {
  if (!value.is_dict()) {
    return false;
  }
  const base::Value::Dict &rect = value.GetDict();
  std::optional<double> left = ReadNumber(rect, "left");
  std::optional<double> top = ReadNumber(rect, "top");
  std::optional<double> width = ReadNumber(rect, "width");
  std::optional<double> height = ReadNumber(rect, "height");
  if (!left || !top || !width || !height || *width <= 0 || *height <= 0) {
    return false;
  }

  base::Value::Dict output;
  output.Set("left", *left);
  output.Set("top", *top);
  output.Set("width", *width);
  output.Set("height", *height);
  if (std::optional<double> right = ReadNumber(rect, "right")) {
    output.Set("right", *right);
  } else {
    output.Set("right", *left + *width);
  }
  if (std::optional<double> bottom = ReadNumber(rect, "bottom")) {
    output.Set("bottom", *bottom);
  } else {
    output.Set("bottom", *top + *height);
  }
  rects->Append(std::move(output));
  return true;
}

base::Value::Dict
BuildOverlayPayload(const base::Value::Dict &params, bool *truncated,
                    std::set<std::string> *missing_element_ids) {
  base::Value::Dict payload;
  payload.Set("style", BuildSanitizedStyle(params));

  base::Value::List output_items;
  const base::Value *items = params.Find("items");
  int max_marks = ReadMaxMarksFromStyle(params);
  int appended_rect_count = 0;
  if (items && items->is_list()) {
    for (const auto &item_value : items->GetList()) {
      if (appended_rect_count >= max_marks) {
        *truncated = true;
        break;
      }
      if (!item_value.is_dict()) {
        continue;
      }
      const base::Value::Dict &item = item_value.GetDict();
      std::string element_id = GetStringField(item, "elementId");
      const base::Value *source_rects = item.Find("rects");
      if (!source_rects || !source_rects->is_list()) {
        if (!element_id.empty() && missing_element_ids) {
          missing_element_ids->insert(element_id);
        }
        continue;
      }

      base::Value::List rects;
      for (const auto &rect_value : source_rects->GetList()) {
        if (appended_rect_count >= max_marks) {
          *truncated = true;
          break;
        }
        if (AppendSanitizedRect(rect_value, &rects)) {
          ++appended_rect_count;
        }
      }
      if (rects.empty()) {
        if (!element_id.empty() && missing_element_ids) {
          missing_element_ids->insert(element_id);
        }
        continue;
      }

      base::Value::Dict output_item;
      output_item.Set("elementId", std::move(element_id));
      output_item.Set("rects", std::move(rects));
      output_items.Append(std::move(output_item));
    }
  }

  payload.Set("items", std::move(output_items));
  return payload;
}

struct OverlayRect {
  double left = 0;
  double top = 0;
  double width = 0;
  double height = 0;

  double Right() const { return left + width; }
  double Bottom() const { return top + height; }
};

struct OverlayPoint {
  double left = 0;
  double top = 0;
};

struct OverlayStyle {
  std::string border_color = "#dc2626";
  double border_width = 1;
  double font_size = 11;
  std::string font_color = "#ffffff";
  std::string background_color = "#dc2626";
  bool avoid_cover = false;
};

struct LabelAnchor {
  std::string text;
  OverlayRect rect;
};

struct OverlayRenderResult {
  int rendered_count = 0;
  std::set<std::string> missing_element_ids;
};

std::string CssPx(double value) { return base::StringPrintf("%.3fpx", value); }

void SetQualifiedAttribute(blink::Element *element,
                           const blink::QualifiedName &name,
                           const std::string &value) {
  element->setAttribute(name, blink::AtomicString::FromUTF8(value.c_str()));
}

void SetAtomicAttribute(blink::Element *element, const char *name,
                        const char *value) {
  element->setAttribute(blink::AtomicString::FromUTF8(name),
                        blink::String::FromUTF8(value));
}

blink::Element *CreateDiv(blink::Document *document) {
  return document ? document->CreateRawElement(blink::html_names::kDivTag)
                  : nullptr;
}

blink::Document *GetCoreDocument(blink::WebLocalFrame *frame) {
  if (!frame) {
    return nullptr;
  }
  blink::WebDocument web_document = frame->GetDocument();
  return web_document;
}

blink::Element *GetOverlayHost(blink::Document *document) {
  if (!document) {
    return nullptr;
  }
  if (blink::Element *document_element = document->documentElement()) {
    return document_element;
  }
  return document->body();
}

bool RemoveOverlayRoot(blink::WebLocalFrame *frame) {
  if (!frame) {
    return false;
  }
  blink::WebElement root = frame->GetDocument().GetElementById(
      blink::WebString::FromUTF8(kOverlayRootId));
  if (root.IsNull()) {
    return false;
  }
  blink::Element *core_root = GetCoreElement(root);
  if (!core_root) {
    return false;
  }
  core_root->remove();
  return true;
}

std::optional<OverlayRect> ReadOverlayRect(const base::Value &value) {
  if (!value.is_dict()) {
    return std::nullopt;
  }
  const base::Value::Dict &dict = value.GetDict();
  std::optional<double> left = ReadNumber(dict, "left");
  std::optional<double> top = ReadNumber(dict, "top");
  std::optional<double> width = ReadNumber(dict, "width");
  std::optional<double> height = ReadNumber(dict, "height");
  if (!left || !top || !width || !height || *width <= 0 || *height <= 0) {
    return std::nullopt;
  }
  return OverlayRect{*left, *top, *width, *height};
}

std::string ReadDictString(const base::Value::Dict *dict, const char *key,
                           const std::string &fallback) {
  if (!dict) {
    return fallback;
  }
  const std::string *value = dict->FindString(key);
  return value && !value->empty() ? *value : fallback;
}

double ReadDictNumber(const base::Value::Dict *dict, const char *key,
                      double fallback) {
  if (!dict) {
    return fallback;
  }
  return ReadNumber(*dict, key).value_or(fallback);
}

OverlayStyle ReadOverlayStyle(const base::Value::Dict &payload) {
  OverlayStyle style;
  const base::Value::Dict *style_dict = payload.FindDict("style");
  const base::Value::Dict *border =
      style_dict ? style_dict->FindDict("border") : nullptr;
  const base::Value::Dict *label =
      style_dict ? style_dict->FindDict("label") : nullptr;
  style.border_color = ReadDictString(border, "color", style.border_color);
  style.border_width =
      std::max(1.0, ReadDictNumber(border, "width", style.border_width));
  style.font_size =
      std::max(8.0, ReadDictNumber(label, "fontSize", style.font_size));
  style.font_color = ReadDictString(label, "fontColor", style.font_color);
  style.background_color =
      ReadDictString(label, "backgroundColor", style.border_color);
  style.avoid_cover =
      label ? label->FindBool("avoidCover").value_or(false) : false;
  return style;
}

OverlayRect BoundsFromPoint(OverlayPoint point, double width, double height) {
  return OverlayRect{point.left, point.top, width, height};
}

std::optional<OverlayRect> ClipRectToViewport(const OverlayRect &rect,
                                              double viewport_width,
                                              double viewport_height) {
  if (viewport_width <= 0 || viewport_height <= 0 ||
      rect.left >= viewport_width || rect.top >= viewport_height ||
      rect.Right() <= 0 || rect.Bottom() <= 0) {
    return std::nullopt;
  }

  double left = std::max(0.0, rect.left);
  double top = std::max(0.0, rect.top);
  double right = std::min(viewport_width, rect.Right());
  double bottom = std::min(viewport_height, rect.Bottom());
  if (right <= left || bottom <= top) {
    return std::nullopt;
  }
  return OverlayRect{left, top, right - left, bottom - top};
}

double IntersectionArea(const OverlayRect &a, const OverlayRect &b) {
  double left = std::max(a.left, b.left);
  double top = std::max(a.top, b.top);
  double right = std::min(a.Right(), b.Right());
  double bottom = std::min(a.Bottom(), b.Bottom());
  return std::max(0.0, right - left) * std::max(0.0, bottom - top);
}

double CollisionScore(const OverlayRect &rect,
                      const std::vector<OverlayRect> &zones, double weight) {
  double score = 0;
  for (const OverlayRect &zone : zones) {
    score += IntersectionArea(rect, zone) * weight;
  }
  return score;
}

std::vector<std::string> BuildCandidateOrder() {
  return {"outsideTopLeft",     "outsideTopRight",  "outsideBottomLeft",
          "outsideBottomRight", "insideTopLeft",    "insideTopRight",
          "insideBottomLeft",   "insideBottomRight"};
}

OverlayPoint MakeCandidate(const std::string &name, const OverlayRect &rect,
                           double width, double height) {
  constexpr double kGap = 4;
  if (name == "outsideTopRight") {
    return {rect.Right() - width, rect.top - height - kGap};
  }
  if (name == "outsideBottomLeft") {
    return {rect.left, rect.Bottom() + kGap};
  }
  if (name == "outsideBottomRight") {
    return {rect.Right() - width, rect.Bottom() + kGap};
  }
  if (name == "insideTopRight") {
    return {rect.Right() - width - kGap, rect.top + kGap};
  }
  if (name == "insideBottomLeft") {
    return {rect.left + kGap, rect.Bottom() - height - kGap};
  }
  if (name == "insideBottomRight") {
    return {rect.Right() - width - kGap, rect.Bottom() - height - kGap};
  }
  if (name == "outsideTopLeft") {
    return {rect.left, rect.top - height - kGap};
  }
  return {rect.left + kGap, rect.top + kGap};
}

double EstimateLabelWidth(const std::string &text, const OverlayStyle &style) {
  double width = static_cast<double>(text.size()) * style.font_size * 0.62 + 10;
  return std::clamp(width, style.font_size + 10, 240.0);
}

double EstimateLabelHeight(const OverlayStyle &style) {
  return style.font_size * 1.4 + 2;
}

OverlayRect PlaceLabel(const OverlayRect &anchor, double label_width,
                       double label_height, double viewport_width,
                       double viewport_height, const OverlayStyle &style,
                       const std::vector<std::string> &candidate_order,
                       const std::vector<OverlayRect> &avoid_zones,
                       const std::vector<OverlayRect> &placed_labels) {
  OverlayPoint best_point;
  OverlayRect best_rect;
  double best_score = std::numeric_limits<double>::infinity();
  bool has_best = false;

  for (const std::string &candidate : candidate_order) {
    OverlayPoint point =
        MakeCandidate(candidate, anchor, label_width, label_height);
    point.left = std::clamp(point.left, 0.0,
                            std::max(0.0, viewport_width - label_width));
    point.top = std::clamp(point.top, 0.0,
                           std::max(0.0, viewport_height - label_height));
    OverlayRect placed = BoundsFromPoint(point, label_width, label_height);
    double score = 0;
    if (style.avoid_cover) {
      score += CollisionScore(placed, avoid_zones, 4);
      score += CollisionScore(placed, placed_labels, 8);
    }
    if (!has_best || score < best_score) {
      has_best = true;
      best_point = point;
      best_rect = placed;
      best_score = score;
    }
    if (!style.avoid_cover || score == 0) {
      break;
    }
  }
  return has_best ? best_rect
                  : BoundsFromPoint(best_point, label_width, label_height);
}

blink::Element *CreateOverlayRoot(blink::Document *document) {
  blink::Element *root = CreateDiv(document);
  if (!root) {
    return nullptr;
  }
  SetQualifiedAttribute(root, blink::html_names::kIdAttr, kOverlayRootId);
  SetAtomicAttribute(root, "aria-hidden", "true");
  SetQualifiedAttribute(
      root, blink::html_names::kStyleAttr,
      "position:fixed;left:0;top:0;width:100vw;height:100vh;"
      "z-index:2147483647;pointer-events:none;contain:layout style paint;");
  return root;
}

void DrawBox(blink::Document *document, blink::Element *root,
             const OverlayRect &rect, const OverlayStyle &style) {
  blink::Element *box = CreateDiv(document);
  if (!box) {
    return;
  }
  std::string css =
      "position:fixed;left:" + CssPx(rect.left) + ";top:" + CssPx(rect.top) +
      ";width:" + CssPx(rect.width) + ";height:" + CssPx(rect.height) +
      ";box-sizing:border-box;border:" + CssPx(style.border_width) + " solid " +
      style.border_color + ";background:transparent;pointer-events:none;";
  SetQualifiedAttribute(box, blink::html_names::kStyleAttr, css);
  root->appendChild(box);
}

void DrawLabel(blink::Document *document, blink::Element *root,
               const LabelAnchor &anchor, const OverlayRect &placement,
               const OverlayStyle &style) {
  blink::Element *label = CreateDiv(document);
  if (!label) {
    return;
  }
  label->setTextContent(blink::String::FromUTF8(anchor.text.c_str()));
  std::string css = "position:fixed;left:" + CssPx(placement.left) +
                    ";top:" + CssPx(placement.top) +
                    ";max-width:240px;box-sizing:border-box;"
                    "padding:1px 5px;background:" +
                    style.background_color + ";color:" + style.font_color +
                    ";font:600 " + CssPx(style.font_size) +
                    "/1.4 sans-serif;white-space:nowrap;overflow:hidden;"
                    "text-overflow:ellipsis;pointer-events:none;";
  SetQualifiedAttribute(label, blink::html_names::kStyleAttr, css);
  root->appendChild(label);
}

OverlayRenderResult RenderOverlayDom(blink::WebLocalFrame *frame,
                                     const base::Value::Dict &payload) {
  OverlayRenderResult result;
  blink::Document *document = GetCoreDocument(frame);
  blink::Element *host = GetOverlayHost(document);
  if (!document || !host) {
    return result;
  }

  RemoveOverlayRoot(frame);
  blink::Element *root = CreateOverlayRoot(document);
  if (!root) {
    return result;
  }
  host->appendChild(root);

  OverlayStyle style = ReadOverlayStyle(payload);
  std::vector<std::string> candidate_order = BuildCandidateOrder();
  std::vector<OverlayRect> avoid_zones;
  std::vector<OverlayRect> placed_labels;
  std::vector<LabelAnchor> labels;

  const base::Value::List *items = payload.FindList("items");
  if (!items) {
    return result;
  }

  blink::Element *viewport_element = document->documentElement();
  double viewport_width =
      viewport_element ? viewport_element->clientWidth() : 0;
  double viewport_height =
      viewport_element ? viewport_element->clientHeight() : 0;
  if (viewport_width <= 0) {
    viewport_width = 1;
  }
  if (viewport_height <= 0) {
    viewport_height = 1;
  }

  for (const base::Value &item_value : *items) {
    if (!item_value.is_dict()) {
      continue;
    }
    const base::Value::Dict &item = item_value.GetDict();
    std::string element_id = GetStringField(item, "elementId");
    const base::Value::List *rects = item.FindList("rects");
    if (!rects) {
      if (!element_id.empty()) {
        result.missing_element_ids.insert(element_id);
      }
      continue;
    }
    std::vector<OverlayRect> clipped_rects;
    double visible_area = 0;
    for (const base::Value &rect_value : *rects) {
      std::optional<OverlayRect> rect = ReadOverlayRect(rect_value);
      if (!rect) {
        continue;
      }
      std::optional<OverlayRect> clipped_rect =
          ClipRectToViewport(*rect, viewport_width, viewport_height);
      if (!clipped_rect) {
        continue;
      }
      visible_area += clipped_rect->width * clipped_rect->height;
      clipped_rects.push_back(*clipped_rect);
    }
    if (clipped_rects.empty() || visible_area < kMinVisibleAnnotationArea) {
      if (!element_id.empty()) {
        result.missing_element_ids.insert(element_id);
      }
      continue;
    }

    for (const OverlayRect &clipped_rect : clipped_rects) {
      DrawBox(document, root, clipped_rect, style);
      avoid_zones.push_back(clipped_rect);
    }
    std::string text = std::to_string(result.rendered_count + 1);
    labels.push_back(LabelAnchor{std::move(text), clipped_rects.front()});
    ++result.rendered_count;
  }

  for (const LabelAnchor &label : labels) {
    double label_width = EstimateLabelWidth(label.text, style);
    double label_height = EstimateLabelHeight(style);
    OverlayRect placement = PlaceLabel(
        label.rect, label_width, label_height, viewport_width, viewport_height,
        style, candidate_order, avoid_zones, placed_labels);
    DrawLabel(document, root, label, placement, style);
    placed_labels.push_back(placement);
  }
  return result;
}

} // namespace

std::string BuildAIPageElementRectsResult(blink::WebLocalFrame *frame,
                                          const std::string &params) {
  if (!frame) {
    return BuildStatusJson(false, "no_frame");
  }
  std::optional<base::Value::Dict> params_dict = ParseParams(params);
  if (!params_dict) {
    return BuildStatusJson(false, "invalid_params");
  }

  base::Value::Dict result;
  result.Set("ok", true);
  std::string current_frame_token =
      NormalizeFrameToken(SerializeFrameToken(frame->GetFrameToken()));
  std::string current_document_scope_token = GetDocumentScopeToken(frame);
  blink::WebDocument document = frame->GetDocument();
  result.Set("frameToken", current_frame_token);
  result.Set("documentScopeToken", current_document_scope_token);

  base::Value::List invalid_ids;
  base::Value::List missing_ids;
  base::Value::List items;

  const base::Value *element_values = params_dict->Find("elementList");
  bool collect_child_frames =
      params_dict->FindBool("collectChildFrames").value_or(true);
  if (!element_values || !element_values->is_list()) {
    result.Set("childFrames", base::Value::List());
    result.Set("items", std::move(items));
    result.Set("invalidElementIds", std::move(invalid_ids));
    result.Set("missingElementIds", std::move(missing_ids));
    return ToJson(std::move(result));
  }

  std::vector<AnnotationRequest> requests;
  std::set<int> wanted_dom_node_ids;

  for (const auto &entry : element_values->GetList()) {
    if (!entry.is_dict()) {
      continue;
    }
    const base::Value::Dict &dict = entry.GetDict();
    std::string element_id = GetStringField(dict, "elementId");
    if (element_id.empty()) {
      continue;
    }
    std::optional<ParsedElementId> parsed = ParseElementId(element_id);
    if (!parsed) {
      invalid_ids.Append(element_id);
      continue;
    }
    if (NormalizeFrameToken(parsed->frame_token) != current_frame_token ||
        parsed->document_scope_token != current_document_scope_token) {
      continue;
    }

    AnnotationRequest request;
    request.element_id = std::move(element_id);
    request.dom_node_id = parsed->dom_node_id;
    wanted_dom_node_ids.insert(request.dom_node_id);
    requests.push_back(std::move(request));
  }

  AnnotationDocumentScanResult scan = ScanAnnotationDocument(
      document, wanted_dom_node_ids, collect_child_frames);
  result.Set("childFrames", std::move(scan.child_frames));
  for (const AnnotationRequest &request : requests) {
    auto found_it = scan.found_elements.find(request.dom_node_id);
    if (found_it == scan.found_elements.end()) {
      missing_ids.Append(request.element_id);
      continue;
    }
    base::Value::List rects = BuildClientRects(found_it->second);
    if (rects.empty()) {
      missing_ids.Append(request.element_id);
      continue;
    }
    base::Value::Dict item;
    item.Set("elementId", request.element_id);
    item.Set("frameToken", current_frame_token);
    item.Set("documentScopeToken", current_document_scope_token);
    item.Set("rects", std::move(rects));
    items.Append(std::move(item));
  }

  result.Set("items", std::move(items));
  result.Set("invalidElementIds", std::move(invalid_ids));
  result.Set("missingElementIds", std::move(missing_ids));
  return ToJson(std::move(result));
}

std::string RenderPageAnnotationOverlay(blink::WebLocalFrame *frame,
                                        const std::string &params) {
  if (!frame) {
    return BuildStatusJson(false, "no_frame");
  }
  std::optional<base::Value::Dict> params_dict = ParseParams(params);
  if (!params_dict) {
    return BuildStatusJson(false, "invalid_params");
  }

  bool truncated = false;
  std::set<std::string> missing_element_ids;
  base::Value::Dict payload =
      BuildOverlayPayload(*params_dict, &truncated, &missing_element_ids);
  OverlayRenderResult render_result = RenderOverlayDom(frame, payload);
  missing_element_ids.insert(render_result.missing_element_ids.begin(),
                             render_result.missing_element_ids.end());

  base::Value::Dict result;
  result.Set("ok", true);
  result.Set("renderedCount", render_result.rendered_count);
  result.Set("truncated", truncated);
  result.Set("missingElementIds", BuildStringList(missing_element_ids));
  return ToJson(std::move(result));
}

std::string RemovePageAnnotationOverlay(blink::WebLocalFrame *frame) {
  if (!frame) {
    return BuildStatusJson(false, "no_frame");
  }

  base::Value::Dict result;
  result.Set("ok", true);
  if (!RemoveOverlayRoot(frame)) {
    return BuildStatusJson(false, "overlay not found");
  }
  return ToJson(std::move(result));
}

} // namespace arkweb::ai_page

#endif // BUILDFLAG(IS_OHOS)
