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

#include "cef/ohos_cef_ext/libcef/renderer/ai_page_dom_collector.h"

#if BUILDFLAG(IS_OHOS)

#include <algorithm>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/contains.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"

#ifndef INSIDE_BLINK
#define INSIDE_BLINK 1
#endif

#include "third_party/blink/public/common/tokens/tokens.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_url.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_frame.h"
#include "third_party/blink/public/web/web_frame_widget.h"
#include "third_party/blink/public/web/web_hit_test_result.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "third_party/blink/public/web/web_view.h"
#include "third_party/blink/renderer/core/dom/attribute.h"
#include "third_party/blink/renderer/core/dom/attribute_collection.h"
#include "third_party/blink/renderer/core/dom/element.h"
#include "third_party/blink/renderer/core/dom/shadow_root.h"
#include "third_party/blink/renderer/core/geometry/dom_rect.h"
#include "third_party/blink/renderer/core/geometry/dom_rect_list.h"
#include "third_party/blink/renderer/core/html/forms/html_input_element.h"
#include "third_party/blink/renderer/core/html/forms/html_option_element.h"
#include "third_party/blink/renderer/core/html/forms/html_select_element.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"

namespace arkweb::ai_page {
namespace {

constexpr char kInternalChildFrameTokenKey[] = "__arkweb_child_frame_token";
constexpr char kInternalChildFramesKey[] = "__arkweb_child_frames";
constexpr char kInternalDocumentScopeTokenKey[] =
    "__arkweb_document_scope_token";
constexpr char kInternalDomNodeIdKey[] = "__arkweb_dom_node_id";

std::string WebStringToString(const blink::WebString& value) {
  return value.IsNull() ? std::string() : value.Utf8();
}

std::string ToLower(std::string_view value) {
  return base::ToLowerASCII(std::string(value));
}

blink::WebString ToWebString(std::string_view value) {
  return blink::WebString::FromUTF8(value);
}

blink::Element* GetCoreElement(const blink::WebElement& element) {
  blink::WebNode node = element;
  return node.Unwrap<blink::Element>();
}

bool HasAttribute(const blink::WebElement& element, std::string_view name) {
  return element.HasAttribute(ToWebString(name));
}

std::string GetAttribute(const blink::WebElement& element,
                         std::string_view name) {
  return WebStringToString(element.GetAttribute(ToWebString(name)));
}

std::string GetTagName(const blink::WebElement& element) {
  return ToLower(WebStringToString(element.TagName()));
}

std::string GetNodeText(const blink::WebNode& node) {
  if (node.IsElementNode()) {
    return std::string();
  }
  return WebStringToString(node.NodeValue());
}

bool IsWhitespaceOnly(std::string_view value) {
  return value.empty() ||
         base::ContainsOnlyChars(value, base::kWhitespaceASCII);
}

std::string GetComputedStyleValue(const blink::WebElement& element,
                                  std::string_view property_name) {
  blink::WebElement mutable_element = element;
  return WebStringToString(
      mutable_element.GetComputedValue(ToWebString(property_name)));
}

bool ShouldSkipElementSubtree(std::string_view tag) {
  static constexpr std::string_view kSkippedTags[] = {
      "script", "noscript", "style", "template", "slot"};
  return base::Contains(kSkippedTags, tag);
}

bool IsInteractiveTag(std::string_view tag) {
  static constexpr std::string_view kInteractiveTags[] = {
      "a",      "audio",   "button", "details", "embed",  "iframe",
      "input",  "label",   "menu",   "menuitem", "object", "optgroup",
      "option", "select",  "summary", "textarea", "video"};
  return base::Contains(kInteractiveTags, tag);
}

bool IsInteractiveRole(std::string_view role) {
  static constexpr std::string_view kInteractiveRoles[] = {
      "a-button-inner", "a-button-text", "a-dropdown-button", "button",
      "button-icon", "button-icon-only", "button-text",
      "button-text-icon-only", "checkbox", "click", "combobox",
      "dropdown", "grid", "link", "listbox", "menu", "menuitem",
      "menuitemcheckbox", "menuitemradio", "option", "progressbar",
      "radio", "scrollbar", "search", "searchbox", "slider", "spinbutton",
      "switch", "tab", "tabpanel", "textbox", "tooltip", "tree",
      "treeitem"};
  return base::Contains(kInteractiveRoles, role);
}

void AddStringToSet(std::string_view value, std::set<std::string>* output) {
  if (!value.empty()) {
    output->insert(ToLower(value));
  }
}

void AddStringsFromValue(const base::Value& value,
                         std::set<std::string>* output) {
  if (value.is_string()) {
    AddStringToSet(value.GetString(), output);
    return;
  }
  if (value.is_list()) {
    for (const auto& item : value.GetList()) {
      AddStringsFromValue(item, output);
    }
    return;
  }
  if (value.is_dict()) {
    for (auto it = value.GetDict().begin(); it != value.GetDict().end();
         ++it) {
      AddStringToSet(it->first, output);
      AddStringsFromValue(it->second, output);
    }
  }
}

std::set<std::string> ReadStringSet(const base::Value* value) {
  std::set<std::string> result;
  if (value) {
    AddStringsFromValue(*value, &result);
  }
  return result;
}

struct Wants {
  std::set<std::string> properties;
  std::set<std::string> attributes;
  bool include_all_attributes = false;
  bool want_tag = false;
  bool want_text = false;
  bool want_title = false;
  bool want_aria_label = false;
  bool want_role = false;
  bool want_aria_description = false;
  bool want_rect = false;
  bool want_bounds = false;
  bool want_position = false;
  bool want_visible = false;
  bool want_in_viewport = false;
  bool want_clickable = false;
  bool want_touchable = false;
  bool want_scrollable = false;
  bool want_inputable = false;
  bool want_url = false;
  bool want_xpath = false;
  bool want_hover = false;
  bool want_mouseover = false;
  bool want_mouseenter = false;
  bool want_select = false;
  bool want_value = false;
  bool want_options = false;
  bool want_value_text = false;
  bool want_select_info = false;
  bool want_focusable = false;
  bool want_editable = false;
  bool want_settable = false;
  bool want_checked = false;
  bool want_expanded = false;
  bool want_pressed = false;
  bool want_selected = false;
  bool want_required = false;
  bool want_autocomplete = false;
  bool want_keyshortcuts = false;
  bool want_node_id = false;
};

void AddWantedProperty(std::string_view property, Wants* wants) {
  AddStringToSet(property, &wants->properties);
}

void AddWantedAttributes(const base::Value::Dict& item, Wants* wants) {
  const base::Value* attributes = item.Find("attributes");
  if (!attributes || !attributes->is_list()) {
    return;
  }

  for (const auto& attribute : attributes->GetList()) {
    if (attribute.is_string()) {
      AddStringToSet(attribute.GetString(), &wants->attributes);
    }
  }
}

void ReadWantsList(const base::Value::List& wants_list, Wants* wants) {
  for (const auto& item : wants_list) {
    if (item.is_string()) {
      AddWantedProperty(item.GetString(), wants);
      continue;
    }
    if (item.is_dict()) {
      AddWantedAttributes(item.GetDict(), wants);
    }
  }
}

void ReadWants(const base::Value* value, Wants* wants) {
  if (!value || !value->is_list()) {
    return;
  }
  ReadWantsList(value->GetList(), wants);
}

bool HasWantedProperty(const Wants& wants, std::string_view property) {
  return wants.properties.find(std::string(property)) != wants.properties.end();
}

void FinalizeWants(Wants* wants) {
  wants->want_tag = HasWantedProperty(*wants, "tag");
  wants->want_text = HasWantedProperty(*wants, "text");
  wants->want_title = HasWantedProperty(*wants, "title");
  wants->want_aria_label = HasWantedProperty(*wants, "aria-label");
  wants->want_role = HasWantedProperty(*wants, "aria-role") ||
                     HasWantedProperty(*wants, "role");
  wants->want_aria_description =
      HasWantedProperty(*wants, "aria-description");
  wants->want_rect = HasWantedProperty(*wants, "rect");
  wants->want_bounds = HasWantedProperty(*wants, "bounds");
  wants->want_position = HasWantedProperty(*wants, "position");
  wants->want_visible = HasWantedProperty(*wants, "visible");
  wants->want_in_viewport = HasWantedProperty(*wants, "isinviewport") ||
                            HasWantedProperty(*wants, "inviewport") ||
                            HasWantedProperty(*wants, "in_viewport");
  wants->want_clickable = HasWantedProperty(*wants, "clickable");
  wants->want_touchable = HasWantedProperty(*wants, "touchable");
  wants->want_scrollable = HasWantedProperty(*wants, "scrollable");
  wants->want_inputable = HasWantedProperty(*wants, "inputable");
  wants->want_url = HasWantedProperty(*wants, "url") ||
                    HasWantedProperty(*wants, "a");
  wants->want_xpath = HasWantedProperty(*wants, "xpath");
  wants->want_hover = HasWantedProperty(*wants, "hover");
  wants->want_mouseover = HasWantedProperty(*wants, "mouseover");
  wants->want_mouseenter = HasWantedProperty(*wants, "mouseenter");
  wants->want_select = HasWantedProperty(*wants, "select");
  wants->want_value = HasWantedProperty(*wants, "value");
  wants->want_options = HasWantedProperty(*wants, "options");
  wants->want_value_text = HasWantedProperty(*wants, "value_text");
  wants->want_select_info = wants->want_select || wants->want_value ||
                            wants->want_options || wants->want_value_text;
  wants->want_focusable = HasWantedProperty(*wants, "focusable");
  wants->want_editable = HasWantedProperty(*wants, "editable");
  wants->want_settable = HasWantedProperty(*wants, "settable");
  wants->want_checked = HasWantedProperty(*wants, "checked");
  wants->want_expanded = HasWantedProperty(*wants, "expanded");
  wants->want_pressed = HasWantedProperty(*wants, "pressed");
  wants->want_selected = HasWantedProperty(*wants, "selected");
  wants->want_required = HasWantedProperty(*wants, "required");
  wants->want_autocomplete = HasWantedProperty(*wants, "autocomplete");
  wants->want_keyshortcuts = HasWantedProperty(*wants, "keyshortcuts");
  wants->want_node_id = HasWantedProperty(*wants, "id") ||
                        HasWantedProperty(*wants, "nodeid") ||
                        HasWantedProperty(*wants, "node_id") ||
                        HasWantedProperty(*wants, "index");
}

Wants ParseWants(const base::Value::Dict& params, bool full_dom) {
  Wants wants;
  ReadWants(params.Find("wants"), &wants);
  wants.properties.insert("tag");
  wants.properties.insert("text");
  if (full_dom) {
    wants.include_all_attributes = true;
  } else {
    wants.properties.insert("xpath");
  }
  FinalizeWants(&wants);
  return wants;
}

bool WantNodeId(const Wants& wants) {
  return wants.want_node_id;
}

base::Value::Dict ParseParams(const std::string& params) {
  std::optional<base::Value> value =
      base::JSONReader::Read(params, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!value || !value->is_dict()) {
    return base::Value::Dict();
  }
  return std::move(value->GetDict());
}

base::Value::Dict BuildRect(const gfx::RectF& rect) {
  base::Value::Dict value;
  value.Set("x", rect.x());
  value.Set("y", rect.y());
  value.Set("left", rect.x());
  value.Set("top", rect.y());
  value.Set("right", rect.right());
  value.Set("bottom", rect.bottom());
  value.Set("width", rect.width());
  value.Set("height", rect.height());
  return value;
}

base::Value::Dict BuildCompactRect(const gfx::RectF& rect) {
  base::Value::Dict value;
  value.Set("x", rect.x());
  value.Set("y", rect.y());
  value.Set("width", rect.width());
  value.Set("height", rect.height());
  return value;
}

void SetNonEmptyString(base::Value::Dict* node,
                       const char* key,
                       std::string value) {
  if (!value.empty()) {
    node->Set(key, std::move(value));
  }
}

struct ElementPropertyCache {
  std::optional<std::string> tag;
  std::optional<gfx::RectF> client_rect;
  std::optional<bool> visible;
  std::optional<bool> in_viewport;
  std::optional<bool> clickable;
  std::optional<bool> scrollable;
  std::optional<std::string> semantic_role;
  std::optional<std::string> cursor;
};

gfx::RectF GetClientRect(const blink::WebElement& element) {
  blink::Element* core_element = GetCoreElement(element);
  if (!core_element) {
    return gfx::RectF();
  }
  return core_element->GetBoundingClientRectNoLifecycleUpdate();
}

std::string GetCachedTagName(const blink::WebElement& element,
                             ElementPropertyCache* cache) {
  if (!cache) {
    return GetTagName(element);
  }
  if (!cache->tag) {
    cache->tag = GetTagName(element);
  }
  return *cache->tag;
}

gfx::RectF GetCachedClientRect(const blink::WebElement& element,
                               ElementPropertyCache* cache) {
  if (!cache) {
    return GetClientRect(element);
  }
  if (!cache->client_rect) {
    cache->client_rect = GetClientRect(element);
  }
  return *cache->client_rect;
}

bool IsVisible(const blink::WebElement& element,
               ElementPropertyCache* cache = nullptr) {
  gfx::RectF rect = GetCachedClientRect(element, cache);
  if (rect.width() <= 0 || rect.height() <= 0) {
    return false;
  }
  std::string display = ToLower(GetComputedStyleValue(element, "display"));
  std::string visibility =
      ToLower(GetComputedStyleValue(element, "visibility"));
  return display != "none" && visibility != "hidden" &&
         visibility != "collapse";
}

bool GetCachedVisible(const blink::WebElement& element,
                      ElementPropertyCache* cache) {
  if (!cache) {
    return IsVisible(element);
  }
  if (!cache->visible) {
    cache->visible = IsVisible(element, cache);
  }
  return *cache->visible;
}

bool HitNodeBelongsToElement(const blink::WebNode& hit_node,
                             const blink::WebElement& element) {
  for (blink::WebNode node = hit_node; !node.IsNull();
       node = node.ParentOrShadowHostNode()) {
    if (node.Equals(element)) {
      return true;
    }
  }
  return false;
}

bool IsPointInViewport(const gfx::Rect& viewport, double x, double y) {
  return x >= viewport.x() && y >= viewport.y() && x < viewport.right() &&
         y < viewport.bottom();
}

double ClampSamplePoint(double value, double min, double max) {
  if (max <= min) {
    return min;
  }
  return std::max(min, std::min(value, max));
}

bool IsInViewport(blink::WebLocalFrame* frame,
                  const blink::WebElement& element,
                  std::optional<bool> known_visible = std::nullopt) {
  constexpr double kMinAgentVisibleRectSize = 5.0;
  constexpr double kMinAgentVisibleArea = 20.0;
  constexpr double kTallElementThreshold = 50.0;
  constexpr double kTallElementSampleInset = 10.0;

  if (!frame || !frame->HasVisibleContent()) {
    return false;
  }
  blink::WebView* web_view = frame->View();
  if (!web_view) {
    return false;
  }
  if (known_visible) {
    if (!*known_visible) {
      return false;
    }
  } else if (!IsVisible(element)) {
    return false;
  }

  gfx::Rect visible = frame->VisibleContentRect();
  gfx::Rect viewport(0, 0, visible.width(), visible.height());
  if (viewport.IsEmpty()) {
    return false;
  }

  blink::Element* core_element = GetCoreElement(element);
  if (!core_element) {
    return false;
  }
  blink::DOMRectList* rects = core_element->getClientRects();
  if (!rects || rects->length() == 0) {
    return false;
  }

  bool has_rect_in_viewport = false;
  bool has_hit_test_result = false;
  blink::WebFrameWidget* widget = frame->FrameWidget();
  for (unsigned i = 0; i < rects->length(); ++i) {
    blink::DOMRect* rect = rects->item(i);
    if (!rect || rect->width() < kMinAgentVisibleRectSize ||
        rect->height() < kMinAgentVisibleRectSize) {
      continue;
    }

    double rect_right = rect->left() + rect->width();
    double rect_bottom = rect->top() + rect->height();
    double clipped_left = std::max<double>(viewport.x(), rect->left());
    double clipped_top = std::max<double>(viewport.y(), rect->top());
    double clipped_right = std::min<double>(viewport.right(), rect_right);
    double clipped_bottom = std::min<double>(viewport.bottom(), rect_bottom);
    double clipped_width = clipped_right - clipped_left;
    double clipped_height = clipped_bottom - clipped_top;
    if (clipped_width <= 0 || clipped_height <= 0 ||
        clipped_width * clipped_height < kMinAgentVisibleArea) {
      continue;
    }
    has_rect_in_viewport = true;

    if (!widget) {
      continue;
    }

    double sample_x = clipped_left + clipped_width / 2.0;
    auto hit_test_sample = [&](double sample_y) {
      gfx::PointF point(sample_x, sample_y);
      if (!IsPointInViewport(viewport, point.x(), point.y())) {
        return false;
      }
      blink::WebHitTestResult hit_result = widget->HitTestResultAt(point);
      if (hit_result.IsNull()) {
        return false;
      }
      has_hit_test_result = true;
      return HitNodeBelongsToElement(hit_result.GetNode(), element);
    };

    if (rect->height() > kTallElementThreshold &&
        clipped_height > kTallElementSampleInset * 2.0) {
      if (hit_test_sample(ClampSamplePoint(
              clipped_top + kTallElementSampleInset, clipped_top,
              clipped_bottom)) ||
          hit_test_sample(clipped_top + clipped_height / 2.0) ||
          hit_test_sample(ClampSamplePoint(
              clipped_bottom - kTallElementSampleInset, clipped_top,
              clipped_bottom))) {
        return true;
      }
    } else if (hit_test_sample(clipped_top + clipped_height / 2.0)) {
      return true;
    }
  }
  return has_rect_in_viewport && !has_hit_test_result;
}

bool GetCachedInViewport(blink::WebLocalFrame* frame,
                         const blink::WebElement& element,
                         ElementPropertyCache* cache) {
  if (!cache) {
    return IsInViewport(frame, element);
  }
  if (!cache->in_viewport) {
    bool visible = GetCachedVisible(element, cache);
    cache->in_viewport = IsInViewport(frame, element, visible);
  }
  return *cache->in_viewport;
}

bool IsScrollable(const blink::WebElement& element) {
  if (GetTagName(element) == "html") {
    return false;
  }
  gfx::Size client = element.GetClientSize();
  gfx::Size scroll = element.GetScrollSize();
  std::string overflowX =
      ToLower(GetComputedStyleValue(element, "overflow-x"));
  std::string overflowY =
      ToLower(GetComputedStyleValue(element, "overflow-y"));
  bool has_scrollable_y_content = scroll.height() > client.height();
  bool overflow_y_scroll = overflowY == "scroll" || overflowY == "auto";
  bool has_scrollable_x_content = scroll.width() > client.width();
  bool overflow_x_scroll = overflowX == "scroll" || overflowX == "auto";

  return (has_scrollable_y_content && overflow_y_scroll) ||
        (has_scrollable_x_content && overflow_x_scroll);
}

bool GetCachedScrollable(const blink::WebElement& element,
                         ElementPropertyCache* cache) {
  if (!cache) {
    return IsScrollable(element);
  }
  if (!cache->scrollable) {
    cache->scrollable = IsScrollable(element);
  }
  return *cache->scrollable;
}

std::string NormalizeEventName(std::string_view event_name) {
  std::string event = ToLower(event_name);
  if (base::StartsWith(event, "on")) {
    event = event.substr(2);
  }
  return event;
}

bool HasInlineEvent(const blink::WebElement& element,
                    std::string_view event_name) {
  std::string event = NormalizeEventName(event_name);
  return HasAttribute(element, "on" + event) || HasAttribute(element, event);
}

bool HasAnyClickHandler(const blink::WebElement& element) {
  static constexpr std::string_view kAttrs[] = {
      "ng-click", "@click", "v-on:click", "contenteditable", "tabindex"};
  for (std::string_view attr : kAttrs) {
    if (HasAttribute(element, attr)) {
      return true;
    }
  }
  static constexpr std::string_view kEvents[] = {
      "click",    "mousedown", "mouseup", "touchstart",
      "touchend", "keydown",   "keyup"};
  for (std::string_view event : kEvents) {
    if (HasInlineEvent(element, event)) {
      return true;
    }
  }
  return false;
}

bool IsInputable(const blink::WebElement& element,
                 ElementPropertyCache* cache = nullptr) {
  std::string tag = GetCachedTagName(element, cache);
  return element.IsEditable() || tag == "input" || tag == "textarea" ||
         tag == "select" || HasAttribute(element, "contenteditable");
}

std::string GetSemanticRole(const blink::WebElement& element) {
  std::string role = ToLower(GetAttribute(element, "role"));
  if (!role.empty()) {
    return role;
  }
  return ToLower(GetAttribute(element, "aria-role"));
}

std::string GetCachedSemanticRole(const blink::WebElement& element,
                                  ElementPropertyCache* cache) {
  if (!cache) {
    return GetSemanticRole(element);
  }
  if (!cache->semantic_role) {
    cache->semantic_role = GetSemanticRole(element);
  }
  return *cache->semantic_role;
}

std::string GetCursorStyle(const blink::WebElement& element,
                           ElementPropertyCache* cache) {
  if (!cache) {
    return GetComputedStyleValue(element, "cursor");
  }
  if (!cache->cursor) {
    cache->cursor = GetComputedStyleValue(element, "cursor");
  }
  return *cache->cursor;
}

bool IsClickable(const blink::WebElement& element,
                 ElementPropertyCache* cache = nullptr) {
  std::string tag = GetCachedTagName(element, cache);
  if (IsInteractiveTag(tag) || HasAnyClickHandler(element)) {
    return true;
  }
  std::string role = GetCachedSemanticRole(element, cache);
  if (IsInteractiveRole(role)) {
    return true;
  }
  std::string cursor = ToLower(GetCursorStyle(element, cache));
  return cursor == "pointer";
}

bool GetCachedClickable(const blink::WebElement& element,
                        ElementPropertyCache* cache) {
  if (!cache) {
    return IsClickable(element);
  }
  if (!cache->clickable) {
    cache->clickable = IsClickable(element, cache);
  }
  return *cache->clickable;
}

std::string GetElementUrl(const blink::WebDocument& document,
                          const blink::WebElement& element) {
  static constexpr std::string_view kUrlAttrs[] = {
      "href", "src", "action", "data", "poster"};
  for (std::string_view attr : kUrlAttrs) {
    std::string value = GetAttribute(element, attr);
    if (!value.empty()) {
      return document.CompleteURL(ToWebString(value)).GetString().Utf8();
    }
  }
  return std::string();
}

std::string GetXPath(const blink::WebElement& element) {
  std::vector<std::string> segments;
  for (blink::WebNode node = element; !node.IsNull();
       node = node.ParentNode()) {
    if (!node.IsElementNode()) {
      continue;
    }
    blink::WebElement current = node.To<blink::WebElement>();
    std::string tag = GetTagName(current);
    if (tag.empty()) {
      continue;
    }
    int index = 1;
    for (blink::WebNode sibling = current.PreviousSibling();
         !sibling.IsNull(); sibling = sibling.PreviousSibling()) {
      if (sibling.IsElementNode()) {
        blink::WebElement siblingElement = sibling.To<blink::WebElement>();
        if (GetTagName(siblingElement) == tag) {
          ++index;
        }
      }
    }
    segments.push_back(tag + "[" + base::NumberToString(index) + "]");
  }

  std::reverse(segments.begin(), segments.end());
  std::string xpath;
  for (const auto& segment : segments) {
    xpath += "/" + segment;
  }
  return xpath.empty() ? "/" : xpath;
}

base::Value::Dict BuildAttributes(const blink::WebElement& element,
                                  const Wants& wants) {
  base::Value::Dict attributes;
  if (wants.include_all_attributes) {
    for (unsigned i = 0; i < element.AttributeCount(); ++i) {
      attributes.Set(WebStringToString(element.AttributeLocalName(i)),
                     WebStringToString(element.AttributeValue(i)));
    }
    return attributes;
  }

  for (const auto& attr : wants.attributes) {
    if (HasAttribute(element, attr)) {
      attributes.Set(attr, GetAttribute(element, attr));
    }
  }
  return attributes;
}

std::string GetFirstAttribute(const blink::WebElement& element,
                              std::initializer_list<std::string_view> names) {
  for (std::string_view name : names) {
    std::string value = GetAttribute(element, name);
    if (!value.empty()) {
      return value;
    }
  }
  return std::string();
}

bool IsTruthyToken(std::string_view value) {
  std::string token = ToLower(value);
  return token == "true" || token == "mixed" || token == "checked" ||
         token == "selected";
}

bool HasTruthyAttribute(const blink::WebElement& element,
                        std::initializer_list<std::string_view> names) {
  for (std::string_view name : names) {
    if (!HasAttribute(element, name)) {
      continue;
    }
    std::string value = GetAttribute(element, name);
    if (value.empty() || IsTruthyToken(value)) {
      return true;
    }
  }
  return false;
}

bool IsEditableElement(const blink::WebElement& element,
                       ElementPropertyCache* cache = nullptr) {
  std::string tag = GetCachedTagName(element, cache);
  return element.IsEditable() || tag == "input" || tag == "textarea" ||
         HasAttribute(element, "contenteditable");
}

bool IsSettableElement(const blink::WebElement& element,
                       ElementPropertyCache* cache = nullptr) {
  if (!IsInputable(element, cache)) {
    return false;
  }
  return !HasAttribute(element, "disabled") && !HasAttribute(element, "readonly");
}

bool IsCheckedElement(const blink::WebElement& element) {
  blink::Element* core_element = GetCoreElement(element);
  if (core_element) {
    if (auto* input = blink::DynamicTo<blink::HTMLInputElement>(core_element)) {
      return input->Checked();
    }
  }
  return HasTruthyAttribute(element, {"checked", "aria-checked"});
}

bool IsExpandedElement(const blink::WebElement& element) {
  return HasTruthyAttribute(element, {"open", "aria-expanded"});
}

bool IsPressedElement(const blink::WebElement& element) {
  return HasTruthyAttribute(element, {"aria-pressed"});
}

bool IsSelectedElement(const blink::WebElement& element) {
  blink::Element* core_element = GetCoreElement(element);
  if (core_element) {
    if (auto* option =
            blink::DynamicTo<blink::HTMLOptionElement>(core_element)) {
      return option->Selected();
    }
  }
  return HasTruthyAttribute(element, {"selected", "aria-selected"});
}

bool IsRequiredElement(const blink::WebElement& element) {
  blink::Element* core_element = GetCoreElement(element);
  if (core_element && core_element->IsRequiredFormControl()) {
    return true;
  }
  return HasTruthyAttribute(element, {"required", "aria-required"});
}

base::Value::List BuildSelectOptions(const blink::WebElement& element) {
  base::Value::List options;
  blink::Element* core_element = GetCoreElement(element);
  if (core_element) {
    if (auto* select =
            blink::DynamicTo<blink::HTMLSelectElement>(core_element)) {
      for (const auto& option : select->GetOptionList()) {
        base::Value::Dict item;
        item.Set("value", option.value().Utf8());
        item.Set("text", option.text().Utf8());
        options.Append(std::move(item));
      }
      return options;
    }
  }

  for (blink::WebNode child = element.FirstChild(); !child.IsNull();
       child = child.NextSibling()) {
    if (!child.IsElementNode()) {
      continue;
    }
    blink::WebElement option = child.To<blink::WebElement>();
    if (GetTagName(option) != "option") {
      continue;
    }
    std::string text = GetNodeText(option);
    std::string value = GetAttribute(option, "value");
    if (value.empty()) {
      value = text;
    }
    base::Value::Dict item;
    item.Set("value", value);
    item.Set("text", text);
    options.Append(std::move(item));
  }
  return options;
}

std::string GetSelectValue(const blink::WebElement& element) {
  blink::Element* core_element = GetCoreElement(element);
  if (core_element) {
    if (auto* select =
            blink::DynamicTo<blink::HTMLSelectElement>(core_element)) {
      std::string value = select->Value().Utf8();
      return value;
    }
  }

  std::string first_value;
  for (blink::WebNode child = element.FirstChild(); !child.IsNull();
       child = child.NextSibling()) {
    if (!child.IsElementNode()) {
      continue;
    }
    blink::WebElement option = child.To<blink::WebElement>();
    if (GetTagName(option) != "option") {
      continue;
    }
    std::string text = GetNodeText(option);
    std::string value = GetAttribute(option, "value");
    if (value.empty()) {
      value = text;
    }
    if (first_value.empty()) {
      first_value = value;
    }
    if (HasAttribute(option, "selected")) {
      return value;
    }
  }
  return first_value;
}

void AddSelectInfo(const blink::WebElement& element,
                   const Wants& wants,
                   const std::string& tag,
                   base::Value::Dict* node) {
  if (tag != "select") {
    return;
  }
  bool want_select = wants.want_select;
  if (want_select || wants.want_value) {
    node->Set("value", GetSelectValue(element));
  }
  if (want_select || wants.want_options) {
    node->Set("options", BuildSelectOptions(element));
  }
  if (wants.want_value_text) {
    node->Set("value_text", BuildSelectOptions(element));
  }
}

void AddWantedProperties(blink::WebLocalFrame* frame,
                         const blink::WebDocument& document,
                         const blink::WebElement& element,
                         const Wants& wants,
                         const std::string* precomputed_xpath,
                         ElementPropertyCache* cache,
                         base::Value::Dict* node) {
  std::string tag = GetCachedTagName(element, cache);
  if (wants.want_tag) {
    node->Set("tag", tag);
  }
  if (wants.want_text) {
    std::string node_text = GetNodeText(element);
    SetNonEmptyString(node, "text", std::move(node_text));
  }
  if (wants.want_title) {
    SetNonEmptyString(node, "title", GetAttribute(element, "title"));
  }
  if (wants.want_aria_label) {
    SetNonEmptyString(node, "aria-label", GetAttribute(element, "aria-label"));
  }
  if (wants.want_role) {
    SetNonEmptyString(node, "role", GetCachedSemanticRole(element, cache));
  }
  if (wants.want_aria_description) {
    SetNonEmptyString(node, "aria-description",
                      GetAttribute(element, "aria-description"));
  }
  bool want_rect = wants.want_rect;
  bool want_bounds = wants.want_bounds;
  bool want_position = wants.want_position;
  if (want_rect || want_bounds || want_position) {
    gfx::RectF client_rect = GetCachedClientRect(element, cache);
    if (want_rect) {
      node->Set("rect", BuildCompactRect(client_rect));
    }
    if (want_bounds) {
      node->Set("bounds", BuildRect(client_rect));
    }
    if (want_position) {
      node->Set("position", BuildRect(client_rect));
    }
  }
  if (wants.want_visible) {
    node->Set("visible", GetCachedVisible(element, cache));
  }
  if (wants.want_in_viewport) {
    node->Set("isInViewport", GetCachedInViewport(frame, element, cache));
  }
  bool want_clickable = wants.want_clickable;
  bool want_touchable = wants.want_touchable;
  if (want_clickable || want_touchable) {
    bool clickable = GetCachedClickable(element, cache);
    if (want_clickable) {
      node->Set("clickable", clickable);
    }
    if (want_touchable) {
      node->Set("touchable", clickable);
    }
  }
  if (wants.want_scrollable) {
    node->Set("scrollable", GetCachedScrollable(element, cache));
  }
  if (wants.want_inputable) {
    node->Set("inputable", IsInputable(element, cache));
  }
  if (wants.want_url) {
    SetNonEmptyString(node, "url", GetElementUrl(document, element));
  }
  if (wants.want_xpath) {
    SetNonEmptyString(node, "xpath",
                      precomputed_xpath && !precomputed_xpath->empty()
                          ? *precomputed_xpath
                          : GetXPath(element));
  }
  if (wants.want_hover) {
    SetNonEmptyString(node, "hover", GetCursorStyle(element, cache));
  }
  if (wants.want_mouseover) {
    node->Set("mouseover", HasInlineEvent(element, "mouseover"));
  }
  if (wants.want_mouseenter) {
    node->Set("mouseenter", HasInlineEvent(element, "mouseenter"));
  }
  if (wants.want_select_info) {
    AddSelectInfo(element, wants, tag, node);
  }
  if (wants.want_focusable) {
    node->Set("focusable", element.IsFocusable());
  }
  if (wants.want_editable) {
    node->Set("editable", IsEditableElement(element, cache));
  }
  if (wants.want_settable) {
    node->Set("settable", IsSettableElement(element, cache));
  }
  if (wants.want_checked) {
    node->Set("checked", IsCheckedElement(element));
  }
  if (wants.want_expanded) {
    node->Set("expanded", IsExpandedElement(element));
  }
  if (wants.want_pressed) {
    node->Set("pressed", IsPressedElement(element));
  }
  if (wants.want_selected) {
    node->Set("selected", IsSelectedElement(element));
  }
  if (wants.want_required) {
    node->Set("required", IsRequiredElement(element));
  }
  if (wants.want_autocomplete) {
    SetNonEmptyString(
        node, "autocomplete",
        GetFirstAttribute(element, {"aria-autocomplete", "autocomplete"}));
  }
  if (wants.want_keyshortcuts) {
    SetNonEmptyString(node, "keyshortcuts",
                      GetAttribute(element, "aria-keyshortcuts"));
  }
  if (wants.include_all_attributes || !wants.attributes.empty()) {
    base::Value::Dict attributes = BuildAttributes(element, wants);
    if (!attributes.empty()) {
      node->Set("attributes", std::move(attributes));
    }
  }
}

struct AttributeRule {
  std::string name;
  std::optional<std::string> contains;
};

struct RuleSet {
  std::set<std::string> tags;
  std::set<std::string> roles;
  std::vector<AttributeRule> attributes;
  std::optional<bool> clickable;
  std::optional<bool> scrollable;
  std::optional<bool> in_viewport;
  bool has_or_rule = false;
};

std::optional<bool> ReadOptionalBool(const base::Value& value) {
  if (value.is_bool()) {
    return value.GetBool();
  }
  if (value.is_int()) {
    return value.GetInt() != 0;
  }
  if (value.is_string()) {
    std::string text = ToLower(value.GetString());
    if (text == "true" || text == "1") {
      return true;
    }
    if (text == "false" || text == "0") {
      return false;
    }
  }
  return std::nullopt;
}

void AddAttributeRulesFromValue(const base::Value& value,
                                RuleSet* rules) {
  if (value.is_dict()) {
    for (auto it = value.GetDict().begin(); it != value.GetDict().end();
         ++it) {
      std::string name = ToLower(it->first);
      if (name.empty()) {
        continue;
      }
      AttributeRule rule{name, std::nullopt};
      if (it->second.is_string() && !it->second.GetString().empty()) {
        rule.contains = ToLower(it->second.GetString());
      }
      rules->attributes.push_back(std::move(rule));
    }
    return;
  }

  std::set<std::string> attributes = ReadStringSet(&value);
  for (const auto& name : attributes) {
    rules->attributes.push_back({name, std::nullopt});
  }
}

void AddRulesFromDict(const base::Value::Dict& dict, RuleSet* rules) {
  for (auto it = dict.begin(); it != dict.end(); ++it) {
    std::string key = ToLower(it->first);
    const base::Value& value = it->second;
    if (key == "tags" || key == "tag") {
      std::set<std::string> tags = ReadStringSet(&value);
      if (!tags.empty()) {
        rules->tags.insert(tags.begin(), tags.end());
        rules->has_or_rule = true;
      }
      continue;
    }
    if (key == "attributes" || key == "attribute") {
      size_t old_size = rules->attributes.size();
      AddAttributeRulesFromValue(value, rules);
      if (rules->attributes.size() != old_size) {
        rules->has_or_rule = true;
      }
      continue;
    }
    if (key == "roles" || key == "role" || key == "ax_role") {
      std::set<std::string> roles = ReadStringSet(&value);
      if (!roles.empty()) {
        rules->roles.insert(roles.begin(), roles.end());
        rules->has_or_rule = true;
      }
      continue;
    }
    if (key == "clickable" || key == "touchable") {
      std::optional<bool> clickable = ReadOptionalBool(value);
      if (clickable) {
        rules->clickable = clickable;
        rules->has_or_rule = true;
      }
      continue;
    }
    if (key == "scrollable") {
      std::optional<bool> scrollable = ReadOptionalBool(value);
      if (scrollable) {
        rules->scrollable = scrollable;
        rules->has_or_rule = true;
      }
      continue;
    }
    if (key == "isinviewport" || key == "inviewport" ||
        key == "in_viewport") {
      std::optional<bool> in_viewport = ReadOptionalBool(value);
      if (in_viewport) {
        rules->in_viewport = in_viewport;
      }
    }
  }
}

RuleSet ParseRules(const base::Value* rules_value) {
  RuleSet rules;
  if (!rules_value) {
    return rules;
  }
  if (rules_value->is_dict()) {
    AddRulesFromDict(rules_value->GetDict(), &rules);
    return rules;
  }
  if (rules_value->is_list()) {
    for (const auto& item : rules_value->GetList()) {
      if (item.is_dict()) {
        AddRulesFromDict(item.GetDict(), &rules);
      }
    }
  }
  return rules;
}

bool MatchAttributeRule(const blink::WebElement& element,
                        const AttributeRule& rule) {
  if (!HasAttribute(element, rule.name)) {
    return false;
  }
  if (!rule.contains) {
    return true;
  }
  std::string value = ToLower(GetAttribute(element, rule.name));
  return value.find(*rule.contains) != std::string::npos;
}

bool MatchesOrRules(const blink::WebElement& element,
                    const RuleSet& rules,
                    ElementPropertyCache* cache) {
  if (!rules.has_or_rule) {
    return true;
  }
  if (!rules.tags.empty() &&
      rules.tags.find(GetCachedTagName(element, cache)) != rules.tags.end()) {
    return true;
  }
  for (const auto& rule : rules.attributes) {
    if (MatchAttributeRule(element, rule)) {
      return true;
    }
  }
  if (!rules.roles.empty() &&
      rules.roles.find(GetCachedSemanticRole(element, cache)) !=
          rules.roles.end()) {
    return true;
  }
  if (rules.clickable &&
      GetCachedClickable(element, cache) == *rules.clickable) {
    return true;
  }
  if (rules.scrollable &&
      GetCachedScrollable(element, cache) == *rules.scrollable) {
    return true;
  }
  return false;
}

bool MatchesRules(blink::WebLocalFrame* frame,
                  const blink::WebElement& element,
                  const RuleSet& rules,
                  ElementPropertyCache* cache) {
  if (rules.in_viewport &&
      GetCachedInViewport(frame, element, cache) != *rules.in_viewport) {
    return false;
  }
  return MatchesOrRules(element, rules, cache);
}

struct CollectContext {
  std::set<std::string> child_frame_tokens;
};

void AddChildFrameTokenIfNeeded(const blink::WebElement& element,
                                CollectContext* context,
                                base::Value::Dict* node);

struct FullDomNode {
  base::Value::Dict data;
  std::vector<std::unique_ptr<FullDomNode>> children;
};

base::Value::Dict MoveToValue(std::unique_ptr<FullDomNode> node) {
  if (!node) {
    return base::Value::Dict();
  }

  struct StackEntry {
    FullDomNode* node;
    bool children_ready;
  };
  std::vector<StackEntry> stack;
  stack.push_back({node.get(), false});
  while (!stack.empty()) {
    StackEntry entry = stack.back();
    stack.pop_back();
    if (!entry.children_ready) {
      stack.push_back({entry.node, true});
      for (auto it = entry.node->children.rbegin();
           it != entry.node->children.rend(); ++it) {
        stack.push_back({it->get(), false});
      }
      continue;
    }

    base::Value::List children;
    for (auto& child : entry.node->children) {
      children.Append(std::move(child->data));
    }
    entry.node->children.clear();
    entry.node->data.Set("children_nodes", std::move(children));
  }
  return std::move(node->data);
}

void AddInternalDomNodeIdIfNeeded(const blink::WebNode& node,
                                  const Wants& wants,
                                  base::Value::Dict* value) {
  if (!WantNodeId(wants)) {
    return;
  }
  int dom_node_id = node.GetDomNodeId();
  if (dom_node_id > 0) {
    value->Set(kInternalDomNodeIdKey, dom_node_id);
  }
}

std::unique_ptr<FullDomNode> BuildElementNode(blink::WebLocalFrame* frame,
                                              const blink::WebDocument& document,
                                              const blink::WebElement& element,
                                              const Wants& wants,
                                              const std::string& xpath,
                                              CollectContext* context) {
  std::string tag = GetTagName(element);
  if (ShouldSkipElementSubtree(tag)) {
    return nullptr;
  }

  auto node = std::make_unique<FullDomNode>();
  AddInternalDomNodeIdIfNeeded(element, wants, &node->data);
  ElementPropertyCache cache;
  cache.tag = tag;
  AddWantedProperties(frame, document, element, wants, &xpath, &cache,
                      &node->data);
  AddChildFrameTokenIfNeeded(element, context, &node->data);
  return node;
}

std::unique_ptr<FullDomNode> BuildTextNode(const blink::WebNode& web_node,
                                           const Wants& wants,
                                           CollectContext* context) {
  std::string text = GetNodeText(web_node);
  if (IsWhitespaceOnly(text) || (!wants.want_tag && !wants.want_text)) {
    return nullptr;
  }

  auto node = std::make_unique<FullDomNode>();
  if (wants.want_tag) {
    node->data.Set("tag", "#text");
  }
  if (wants.want_text) {
    node->data.Set("text", text);
  }
  return node;
}

std::string ShadowRootModeToString(blink::ShadowRootMode mode) {
  switch (mode) {
    case blink::ShadowRootMode::kOpen:
      return "open";
    case blink::ShadowRootMode::kClosed:
      return "closed";
    case blink::ShadowRootMode::kUserAgent:
      return "user-agent";
  }
  return std::string();
}

std::unique_ptr<FullDomNode> BuildShadowRootNode(
    const blink::WebElement& host,
    const Wants& wants,
    CollectContext* context) {
  blink::Element* core_element = GetCoreElement(host);
  blink::ShadowRoot* shadow_root =
      core_element ? core_element->GetShadowRoot() : nullptr;
  if (!shadow_root || shadow_root->IsUserAgent()) {
    return nullptr;
  }

  auto node = std::make_unique<FullDomNode>();
  if (wants.want_tag) {
    node->data.Set("tag", "#shadowRoot");
  }
  if (wants.include_all_attributes ||
      wants.attributes.find("mode") != wants.attributes.end()) {
    base::Value::Dict attributes;
    attributes.Set("mode", ShadowRootModeToString(shadow_root->GetMode()));
    node->data.Set("attributes", std::move(attributes));
  }
  return node;
}

std::string SerializeFrameToken(const blink::FrameToken& token) {
  if (token.Is<blink::LocalFrameToken>()) {
    return std::string("local:") +
           token.GetAs<blink::LocalFrameToken>().ToString();
  }
  return std::string("remote:") +
         token.GetAs<blink::RemoteFrameToken>().ToString();
}

std::optional<std::string> GetChildFrameToken(
    const blink::WebElement& element) {
  blink::WebFrame* child_frame = blink::WebFrame::FromFrameOwnerElement(element);
  if (!child_frame) {
    return std::nullopt;
  }
  return SerializeFrameToken(child_frame->GetFrameToken());
}

void AddChildFrameTokenIfNeeded(const blink::WebElement& element,
                                CollectContext* context,
                                base::Value::Dict* node) {
  std::optional<std::string> token = GetChildFrameToken(element);
  if (!token) {
    return;
  }
  context->child_frame_tokens.insert(*token);
  node->Set(kInternalChildFrameTokenKey, *token);
}

void AddChildFrameTokenToNodeIfNeeded(const std::optional<std::string>& token,
                                      base::Value::Dict* node) {
  if (token) {
    node->Set(kInternalChildFrameTokenKey, *token);
  }
}

std::optional<std::string> AddChildFrameTokenToContext(
    const blink::WebElement& element,
    CollectContext* context) {
  std::optional<std::string> token = GetChildFrameToken(element);
  if (token) {
    context->child_frame_tokens.insert(*token);
  }
  return token;
}

struct FullDomTraversalEntry {
  blink::WebNode node;
  blink::WebDocument document;
  blink::WebLocalFrame* frame;
  FullDomNode* parent;
  std::string xpath;
};

struct LiteDomTraversalEntry {
  blink::WebNode node;
  blink::WebDocument document;
  blink::WebLocalFrame* frame;
  std::string xpath;
};

std::string BuildXPathSegment(std::string_view parent_xpath,
                              std::string_view tag,
                              int index) {
  if (tag.empty()) {
    return std::string();
  }
  return std::string(parent_xpath) + "/" + std::string(tag) + "[" +
         base::NumberToString(index) + "]";
}

std::string GetRootXPath(blink::WebNode root, bool build_xpath) {
  if (!build_xpath || !root.IsElementNode()) {
    return std::string();
  }
  return BuildXPathSegment(std::string_view(),
                           GetTagName(root.To<blink::WebElement>()), 1);
}

std::string GetChildXPathForReverseTraversal(
    blink::WebNode child,
    std::string_view parent_xpath,
    std::map<std::string, int>* tag_counts,
    bool build_xpath) {
  if (!build_xpath || !child.IsElementNode()) {
    return std::string();
  }
  std::string tag = GetTagName(child.To<blink::WebElement>());
  if (tag.empty()) {
    return std::string();
  }
  int index = (*tag_counts)[tag]--;
  return BuildXPathSegment(parent_xpath, tag, index);
}

void CountElementChildrenByTag(const blink::WebNode& parent,
                               std::map<std::string, int>* tag_counts) {
  for (blink::WebNode child = parent.FirstChild(); !child.IsNull();
       child = child.NextSibling()) {
    if (!child.IsElementNode()) {
      continue;
    }
    std::string tag = GetTagName(child.To<blink::WebElement>());
    if (!tag.empty()) {
      ++(*tag_counts)[tag];
    }
  }
}

void PushChildrenInReverse(const blink::WebNode& parent,
                           const blink::WebDocument& document,
                           blink::WebLocalFrame* frame,
                           FullDomNode* output,
                           std::string_view parent_xpath,
                           bool build_xpath,
                           std::vector<FullDomTraversalEntry>* stack) {
  std::optional<std::map<std::string, int>> tag_counts;
  if (build_xpath) {
    tag_counts.emplace();
    CountElementChildrenByTag(parent, &*tag_counts);
  }
  for (blink::WebNode child = parent.LastChild(); !child.IsNull();
       child = child.PreviousSibling()) {
    std::string child_xpath = build_xpath
                                  ? GetChildXPathForReverseTraversal(
                                        child, parent_xpath, &*tag_counts, true)
                                  : std::string();
    stack->push_back({child, document, frame, output, std::move(child_xpath)});
  }
}

void PushLiteChildrenInReverse(const blink::WebNode& parent,
                               const blink::WebDocument& document,
                               blink::WebLocalFrame* frame,
                               std::string_view parent_xpath,
                               bool build_xpath,
                               std::vector<LiteDomTraversalEntry>* stack) {
  std::optional<std::map<std::string, int>> tag_counts;
  if (build_xpath) {
    tag_counts.emplace();
    CountElementChildrenByTag(parent, &*tag_counts);
  }
  for (blink::WebNode child = parent.LastChild(); !child.IsNull();
       child = child.PreviousSibling()) {
    std::string child_xpath = build_xpath
                                  ? GetChildXPathForReverseTraversal(
                                        child, parent_xpath, &*tag_counts, true)
                                  : std::string();
    stack->push_back({child, document, frame, std::move(child_xpath)});
  }
}

void CollectFullDom(blink::WebLocalFrame* frame,
                    const blink::WebDocument& document,
                    const blink::WebNode& root,
                    const Wants& wants,
                    CollectContext* context,
                    std::vector<std::unique_ptr<FullDomNode>>* roots) {
  bool build_xpath = wants.want_xpath;
  std::vector<FullDomTraversalEntry> stack;
  stack.push_back({root, document, frame, nullptr,
                   GetRootXPath(root, build_xpath)});

  while (!stack.empty()) {
    FullDomTraversalEntry entry = std::move(stack.back());
    stack.pop_back();
    blink::WebNode web_node = entry.node;
    blink::WebDocument current_document = entry.document;
    blink::WebLocalFrame* current_frame = entry.frame;
    FullDomNode* parent = entry.parent;
    std::unique_ptr<FullDomNode> output_node;

    if (web_node.IsElementNode()) {
      blink::WebElement element = web_node.To<blink::WebElement>();
      output_node = BuildElementNode(current_frame, current_document, element,
                                     wants, entry.xpath, context);
      if (!output_node) {
        continue;
      }
    } else if (web_node.IsTextNode()) {
      output_node = BuildTextNode(web_node, wants, context);
      if (!output_node) {
        continue;
      }
    } else {
      continue;
    }

    FullDomNode* output_ptr = output_node.get();
    if (parent) {
      parent->children.push_back(std::move(output_node));
    } else {
      roots->push_back(std::move(output_node));
    }

    if (!web_node.IsElementNode()) {
      continue;
    }

    blink::WebElement element = web_node.To<blink::WebElement>();
    PushChildrenInReverse(web_node, current_document, current_frame, output_ptr,
                          entry.xpath, build_xpath, &stack);

    blink::WebNode shadow_root = element.OpenOrClosedShadowRoot();
    std::unique_ptr<FullDomNode> shadow_output =
        shadow_root.IsNull() ? nullptr
                             : BuildShadowRootNode(element, wants, context);
    if (!shadow_output) {
      continue;
    }
    FullDomNode* shadow_ptr = shadow_output.get();
    output_ptr->children.push_back(std::move(shadow_output));
    PushChildrenInReverse(shadow_root, current_document, current_frame,
                          shadow_ptr, std::string_view(), build_xpath,
                          &stack);
  }
}

base::Value::Dict BuildLiteElementNode(blink::WebLocalFrame* frame,
                                       const blink::WebDocument& document,
                                       const blink::WebElement& element,
                                       const Wants& wants,
                                       const std::string& xpath,
                                       ElementPropertyCache* cache,
                                       const std::optional<std::string>& frame_token) {
  base::Value::Dict node;
  AddInternalDomNodeIdIfNeeded(element, wants, &node);
  AddWantedProperties(frame, document, element, wants, &xpath, cache, &node);
  AddChildFrameTokenToNodeIfNeeded(frame_token, &node);
  return node;
}

void PushElementTraversalChildren(blink::WebElement element,
                                  const blink::WebDocument& document,
                                  blink::WebLocalFrame* frame,
                                  std::string_view parent_xpath,
                                  bool build_xpath,
                                  std::vector<LiteDomTraversalEntry>* stack) {
  PushLiteChildrenInReverse(element, document, frame, parent_xpath, build_xpath,
                            stack);

  blink::WebNode shadow_root = element.OpenOrClosedShadowRoot();
  if (!shadow_root.IsNull()) {
    stack->push_back({shadow_root, document, frame, std::string()});
  }
}

void CollectLiteDom(blink::WebLocalFrame* frame,
                    const blink::WebDocument& document,
                    const blink::WebNode& root,
                    const Wants& wants,
                    const RuleSet& rules,
                    CollectContext* context,
                    base::Value::List* nodes) {
  bool build_xpath = wants.want_xpath;
  std::vector<LiteDomTraversalEntry> stack;
  stack.push_back({root, document, frame, GetRootXPath(root, build_xpath)});

  while (!stack.empty()) {
    LiteDomTraversalEntry entry = std::move(stack.back());
    stack.pop_back();
    blink::WebNode web_node = entry.node;
    blink::WebDocument current_document = entry.document;
    blink::WebLocalFrame* current_frame = entry.frame;

    if (!web_node.IsElementNode()) {
      PushLiteChildrenInReverse(web_node, current_document, current_frame,
                                entry.xpath, build_xpath, &stack);
      continue;
    }

    blink::WebElement element = web_node.To<blink::WebElement>();
    std::string tag = GetTagName(element);
    if (ShouldSkipElementSubtree(tag)) {
      continue;
    }

    std::optional<std::string> child_frame_token =
        AddChildFrameTokenToContext(element, context);

    ElementPropertyCache cache;
    cache.tag = tag;
    if (MatchesRules(current_frame, element, rules, &cache)) {
      nodes->Append(
          BuildLiteElementNode(current_frame, current_document, element, wants,
                               entry.xpath, &cache, child_frame_token));
    }
    PushElementTraversalChildren(element, current_document, current_frame,
                                 entry.xpath, build_xpath, &stack);
  }
}

void AddInternalFrameMetadata(const CollectContext& context,
                              blink::WebLocalFrame* frame,
                              base::Value::Dict* result) {
  if (frame && frame->GetEmbeddingToken()) {
    result->Set(kInternalDocumentScopeTokenKey,
                frame->GetEmbeddingToken()->ToString());
  }
  if (context.child_frame_tokens.empty()) {
    return;
  }
  base::Value::List child_frames;
  for (const std::string& token : context.child_frame_tokens) {
    child_frames.Append(token);
  }
  result->Set(kInternalChildFramesKey, std::move(child_frames));
}

}  // namespace

std::string BuildFullDomResult(blink::WebLocalFrame* frame,
                               const std::string& params) {
  if (!frame) {
    return std::string();
  }

  base::Value::Dict paramsDict = ParseParams(params);
  Wants wants = ParseWants(paramsDict, true);
  CollectContext context;

  blink::WebDocument document = frame->GetDocument();
  base::Value::Dict result;
  result.Set("url", document.Url().GetString().Utf8());
  result.Set("title", WebStringToString(document.Title()));

  base::Value::List children;
  std::vector<std::unique_ptr<FullDomNode>> root_nodes;
  blink::WebElement root = document.DocumentElement();
  if (!root.IsNull()) {
    CollectFullDom(frame, document, root, wants, &context, &root_nodes);
  }
  for (auto& root_node : root_nodes) {
    children.Append(MoveToValue(std::move(root_node)));
  }
  result.Set("children_nodes", std::move(children));
  AddInternalFrameMetadata(context, frame, &result);

  std::string json;
  base::JSONWriter::Write(result, &json);
  return json;
}

std::string BuildLiteDomResult(blink::WebLocalFrame* frame,
                               const std::string& params) {
  if (!frame) {
    return std::string();
  }

  base::Value::Dict paramsDict = ParseParams(params);
  Wants wants = ParseWants(paramsDict, false);
  RuleSet rules = ParseRules(paramsDict.Find("rules"));
  CollectContext context;

  blink::WebDocument document = frame->GetDocument();
  base::Value::Dict result;
  result.Set("url", document.Url().GetString().Utf8());
  result.Set("title", WebStringToString(document.Title()));

  base::Value::List nodes;
  blink::WebElement root = document.DocumentElement();
  if (!root.IsNull()) {
    CollectLiteDom(frame, document, root, wants, rules, &context, &nodes);
  }
  result.Set("nodes", std::move(nodes));
  AddInternalFrameMetadata(context, frame, &result);

  std::string json;
  base::JSONWriter::Write(result, &json);
  return json;
}

}  // namespace arkweb::ai_page

#endif  // BUILDFLAG(IS_OHOS)
