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

#include "cef/ohos_cef_ext/libcef/browser/ai_page_element_rect_resolver.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/containers/contains.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "cef/libcef/browser/browser_info.h"
#include "cef/libcef/browser/frame_host_impl.h"
#include "cef/libcef/browser/thread_util.h"
#include "cef/ohos_cef_ext/libcef/browser/arkweb_frame_host_impl_ext.h"
#include "content/public/browser/global_routing_id.h"
#include "content/public/browser/render_frame_host.h"

#if BUILDFLAG(IS_OHOS)
namespace arkweb::ai_page {
namespace {

constexpr base::TimeDelta kElementRectResolveTimeout = base::Milliseconds(5000);

std::string ToJson(base::Value::Dict result) {
  std::string json;
  base::JSONWriter::Write(result, &json);
  return json;
}

std::optional<base::Value::Dict> ParseJsonDict(const std::string &json) {
  std::optional<base::Value> value =
      base::JSONReader::Read(json, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!value || !value->is_dict()) {
    return std::nullopt;
  }
  return std::move(value->GetDict());
}

void AddStringValues(const base::Value::List *source,
                     std::set<std::string> *target) {
  if (!source || !target) {
    return;
  }
  for (const base::Value &value : *source) {
    if (value.is_string()) {
      target->insert(value.GetString());
    }
  }
}

std::string NormalizeFrameTokenString(const std::string &token) {
  static constexpr char kLocalPrefix[] = "local:";
  static constexpr char kRemotePrefix[] = "remote:";
  if (token.rfind(kLocalPrefix, 0) == 0) {
    return token.substr(sizeof(kLocalPrefix) - 1);
  }
  if (token.rfind(kRemotePrefix, 0) == 0) {
    return token.substr(sizeof(kRemotePrefix) - 1);
  }
  return token;
}

std::string GetFrameToken(const CefRefPtr<ArkwebFrameHostExtImpl> &frame) {
  if (!frame || !frame->frame_token()) {
    return std::string();
  }
  return frame->frame_token()->frame_token.ToString();
}

std::string
GetParentFrameToken(const CefRefPtr<ArkwebFrameHostExtImpl> &frame) {
  if (!frame || !frame->GetRenderFrameHost() ||
      !frame->GetRenderFrameHost()->GetParent()) {
    return std::string();
  }
  return frame->GetRenderFrameHost()
      ->GetParent()
      ->GetGlobalFrameToken()
      .frame_token.ToString();
}

std::optional<std::string>
ParseFrameTokenFromElementId(const std::string &element_id) {
  size_t dom_separator = element_id.rfind('|');
  if (dom_separator == std::string::npos ||
      dom_separator + 1 >= element_id.size()) {
    return std::nullopt;
  }
  size_t document_separator = element_id.rfind('|', dom_separator - 1);
  if (document_separator == std::string::npos || document_separator == 0 ||
      document_separator + 1 == dom_separator) {
    return std::nullopt;
  }
  int dom_node_id = 0;
  if (!base::StringToInt(element_id.substr(dom_separator + 1), &dom_node_id) ||
      dom_node_id <= 0) {
    return std::nullopt;
  }
  return NormalizeFrameTokenString(element_id.substr(0, document_separator));
}

std::optional<double> ReadNumberField(const base::Value::Dict &dict,
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

bool AppendTranslatedRect(const base::Value &value, double x_offset,
                          double y_offset, base::Value::List *rects) {
  if (!value.is_dict()) {
    return false;
  }
  const base::Value::Dict &rect = value.GetDict();
  std::optional<double> left = ReadNumberField(rect, "left");
  std::optional<double> top = ReadNumberField(rect, "top");
  std::optional<double> width = ReadNumberField(rect, "width");
  std::optional<double> height = ReadNumberField(rect, "height");
  if (!left || !top || !width || !height || *width <= 0 || *height <= 0) {
    return false;
  }

  base::Value::Dict output;
  output.Set("left", *left + x_offset);
  output.Set("top", *top + y_offset);
  output.Set("width", *width);
  output.Set("height", *height);
  output.Set("right", *left + x_offset + *width);
  output.Set("bottom", *top + y_offset + *height);
  rects->Append(std::move(output));
  return true;
}

void AddFrame(CefRefPtr<CefFrameHostImpl> frame,
              std::set<content::GlobalRenderFrameHostToken> *seen_frames,
              std::vector<CefRefPtr<ArkwebFrameHostExtImpl>> *frames) {
  if (!frame || !frame->frame_token() || !frame->GetRenderFrameHost()) {
    return;
  }
  if (!seen_frames->insert(*frame->frame_token()).second) {
    return;
  }
  if (!frame->AsArkWebFrame()) {
    return;
  }
  frames->push_back(static_cast<ArkwebFrameHostExtImpl *>(frame.get()));
}

std::vector<CefRefPtr<ArkwebFrameHostExtImpl>>
CollectFrames(scoped_refptr<CefBrowserInfo> browser_info,
              CefRefPtr<ArkwebFrameHostExtImpl> fallback_frame) {
  std::vector<CefRefPtr<ArkwebFrameHostExtImpl>> frames;
  std::set<content::GlobalRenderFrameHostToken> seen_frames;
  if (browser_info) {
    AddFrame(browser_info->GetMainFrame(), &seen_frames, &frames);
    for (const auto &frame : browser_info->GetAllFrames()) {
      AddFrame(frame, &seen_frames, &frames);
    }
  }
  AddFrame(CefRefPtr<CefFrameHostImpl>(fallback_frame.get()), &seen_frames,
           &frames);
  return frames;
}

} // namespace

AIPageElementRectResolveResult::AIPageElementRectResolveResult() = default;
AIPageElementRectResolveResult::AIPageElementRectResolveResult(
    AIPageElementRectResolveResult &&) = default;
AIPageElementRectResolveResult &AIPageElementRectResolveResult::operator=(
    AIPageElementRectResolveResult &&) = default;
AIPageElementRectResolveResult::~AIPageElementRectResolveResult() = default;

// static
void AIPageElementRectResolver::Resolve(
    scoped_refptr<CefBrowserInfo> browser_info,
    CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
    std::vector<std::string> element_ids, int max_rects,
    AIPageElementRectResolveCallback callback) {
  if (!CEF_CURRENTLY_ON_UIT()) {
    CEF_POST_TASK(CEF_UIT, base::BindOnce(&AIPageElementRectResolver::Resolve,
                                          std::move(browser_info), root_frame,
                                          std::move(element_ids), max_rects,
                                          std::move(callback)));
    return;
  }

  scoped_refptr<AIPageElementRectResolver> resolver =
      new AIPageElementRectResolver(std::move(browser_info), root_frame,
                                    std::move(element_ids), max_rects,
                                    std::move(callback));
  resolver->Start();
}

AIPageElementRectResolver::AIPageElementRectResolver(
    scoped_refptr<CefBrowserInfo> browser_info,
    CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
    std::vector<std::string> element_ids, int max_rects,
    AIPageElementRectResolveCallback callback)
    : browser_info_(std::move(browser_info)), root_frame_(root_frame),
      element_ids_(std::move(element_ids)), max_rects_(std::max(1, max_rects)),
      callback_(std::move(callback)) {}

AIPageElementRectResolver::~AIPageElementRectResolver() = default;

void AIPageElementRectResolver::Start() {
  CEF_REQUIRE_UIT();

  result_.requested_count = static_cast<int>(element_ids_.size());
  BuildElementRoutingState();

  frames_ = CollectFrames(browser_info_, root_frame_);
  if (!frames_.empty()) {
    top_frame_ = frames_.front();
    top_frame_token_ = GetFrameToken(top_frame_);
    result_.top_frame = top_frame_;
  }
  if (frames_.empty() || !top_frame_) {
    CompleteWithResult();
    return;
  }

  BuildFrameRoutingState();
  StartCapture();
}

void AIPageElementRectResolver::BuildElementRoutingState() {
  for (const std::string &element_id : element_ids_) {
    if (element_id.empty()) {
      continue;
    }
    requested_ids_.insert(element_id);
    std::optional<std::string> target_frame_token =
        ParseFrameTokenFromElementId(element_id);
    if (!target_frame_token) {
      result_.invalid_element_ids.insert(element_id);
      continue;
    }
    target_frame_tokens_.insert(*target_frame_token);
    base::Value::Dict element;
    element.Set("elementId", element_id);
    elements_by_frame_token_[*target_frame_token].Append(std::move(element));
  }
}

void AIPageElementRectResolver::BuildFrameRoutingState() {
  frame_parent_tokens_.clear();
  frame_tree_probe_tokens_.clear();

  for (const auto &frame : frames_) {
    std::string frame_token = GetFrameToken(frame);
    if (frame_token.empty()) {
      continue;
    }
    std::string parent_frame_token = GetParentFrameToken(frame);
    if (!parent_frame_token.empty()) {
      frame_parent_tokens_[frame_token] = parent_frame_token;
    }
  }

  for (const std::string &target_frame_token : target_frame_tokens_) {
    std::string current = target_frame_token;
    std::set<std::string> visited;
    while (!current.empty() && current != top_frame_token_ &&
           visited.insert(current).second) {
      auto parent_it = frame_parent_tokens_.find(current);
      if (parent_it == frame_parent_tokens_.end()) {
        break;
      }
      frame_tree_probe_tokens_.insert(parent_it->second);
      current = parent_it->second;
    }
  }
}

bool AIPageElementRectResolver::BuildFrameCaptureParams(
    const std::string &frame_token, std::string *params) const {
  bool collect_child_frames =
      base::Contains(frame_tree_probe_tokens_, frame_token);
  auto elements_it = elements_by_frame_token_.find(frame_token);
  bool has_target_elements = elements_it != elements_by_frame_token_.end() &&
                             !elements_it->second.empty();
  if (!collect_child_frames && !has_target_elements) {
    return false;
  }

  base::Value::Dict capture_params;
  capture_params.Set("collectChildFrames", collect_child_frames);
  capture_params.Set("elementList", has_target_elements
                                        ? elements_it->second.Clone()
                                        : base::Value::List());
  *params = ToJson(std::move(capture_params));
  return true;
}

void AIPageElementRectResolver::StartCapture() {
  CEF_REQUIRE_UIT();

  if (completed_) {
    return;
  }

  std::vector<std::pair<size_t, std::string>> frame_requests;
  for (size_t index = 0; index < frames_.size(); ++index) {
    std::string frame_token = GetFrameToken(frames_[index]);
    std::string capture_params;
    if (!BuildFrameCaptureParams(frame_token, &capture_params)) {
      continue;
    }
    frame_requests.emplace_back(index, std::move(capture_params));
  }

  pending_count_ = frame_requests.size();
  if (pending_count_ == 0) {
    CompleteWithResult();
    return;
  }
  StartTimer();
  for (const auto &request : frame_requests) {
    frames_[request.first]->GetAIPageElementRectsWithResponse(
        request.second,
        base::BindOnce(&AIPageElementRectResolver::OnRectsCaptured,
                       base::WrapRefCounted(this), request.first));
  }
}

void AIPageElementRectResolver::AddChildFrameOffsets(
    const std::string &parent_frame_token,
    const base::Value::List *child_frames) {
  if (parent_frame_token.empty() || !child_frames) {
    return;
  }

  for (const base::Value &child_value : *child_frames) {
    if (!child_value.is_dict()) {
      continue;
    }
    const base::Value::Dict &child = child_value.GetDict();
    const std::string *child_frame_token = child.FindString("frameToken");
    const base::Value::Dict *rect = child.FindDict("rect");
    if (!child_frame_token || child_frame_token->empty() || !rect) {
      continue;
    }
    std::optional<double> left = ReadNumberField(*rect, "left");
    std::optional<double> top = ReadNumberField(*rect, "top");
    if (!left || !top) {
      continue;
    }
    child_frame_offsets_[NormalizeFrameTokenString(*child_frame_token)] =
        ChildFrameOffset{parent_frame_token, *left, *top};
  }
}

void AIPageElementRectResolver::OnRectsCaptured(size_t frame_index,
                                                const std::string &result) {
  CEF_REQUIRE_UIT();

  if (completed_) {
    return;
  }

  std::optional<base::Value::Dict> result_dict = ParseJsonDict(result);
  if (result_dict) {
    std::string frame_token;
    if (frame_index < frames_.size()) {
      frame_token = GetFrameToken(frames_[frame_index]);
    }
    if (const std::string *result_frame_token =
            result_dict->FindString("frameToken")) {
      frame_token = NormalizeFrameTokenString(*result_frame_token);
    }

    AddStringValues(result_dict->FindList("invalidElementIds"),
                    &result_.invalid_element_ids);
    AddStringValues(result_dict->FindList("missingElementIds"),
                    &result_.missing_element_ids);
    AddChildFrameOffsets(frame_token, result_dict->FindList("childFrames"));

    const base::Value::List *items = result_dict->FindList("items");
    if (items) {
      for (const base::Value &item : *items) {
        if (!item.is_dict()) {
          continue;
        }
        base::Value::Dict item_copy = item.GetDict().Clone();
        const std::string *item_frame_token =
            item_copy.FindString("frameToken");
        if ((!item_frame_token || item_frame_token->empty()) &&
            !frame_token.empty()) {
          item_copy.Set("frameToken", frame_token);
        }
        captured_items_.Append(std::move(item_copy));
      }
    }
  }

  if (pending_count_ > 0) {
    --pending_count_;
  }
  if (pending_count_ == 0) {
    command_timeout_timer_.Stop();
    CompleteWithResult();
  }
}

bool AIPageElementRectResolver::ResolveFrameOffsetInternal(
    const std::string &frame_token, std::set<std::string> *visiting,
    double *x_offset, double *y_offset) const {
  if (frame_token.empty()) {
    return false;
  }
  if (frame_token == top_frame_token_) {
    *x_offset = 0;
    *y_offset = 0;
    return true;
  }
  if (!visiting->insert(frame_token).second) {
    return false;
  }
  auto offset_it = child_frame_offsets_.find(frame_token);
  if (offset_it == child_frame_offsets_.end()) {
    return false;
  }

  double parent_x_offset = 0;
  double parent_y_offset = 0;
  if (!ResolveFrameOffsetInternal(offset_it->second.parent_frame_token,
                                  visiting, &parent_x_offset,
                                  &parent_y_offset)) {
    return false;
  }
  *x_offset = parent_x_offset + offset_it->second.left;
  *y_offset = parent_y_offset + offset_it->second.top;
  return true;
}

bool AIPageElementRectResolver::ResolveFrameOffset(
    const std::string &frame_token, double *x_offset, double *y_offset) const {
  std::set<std::string> visiting;
  return ResolveFrameOffsetInternal(NormalizeFrameTokenString(frame_token),
                                    &visiting, x_offset, y_offset);
}

void AIPageElementRectResolver::BuildTopFrameItems() {
  int appended_rect_count = 0;

  for (const base::Value &item_value : captured_items_) {
    if (appended_rect_count >= max_rects_) {
      result_.truncated = true;
      break;
    }
    if (!item_value.is_dict()) {
      continue;
    }
    const base::Value::Dict &item = item_value.GetDict();
    const std::string *element_id = item.FindString("elementId");
    if (!element_id || element_id->empty()) {
      continue;
    }
    const std::string *item_frame_token = item.FindString("frameToken");
    double x_offset = 0;
    double y_offset = 0;
    if (!item_frame_token ||
        !ResolveFrameOffset(*item_frame_token, &x_offset, &y_offset)) {
      unresolved_element_ids_.insert(*element_id);
      continue;
    }

    const base::Value::List *source_rects = item.FindList("rects");
    if (!source_rects) {
      continue;
    }
    base::Value::List rects;
    for (const base::Value &rect_value : *source_rects) {
      if (appended_rect_count >= max_rects_) {
        result_.truncated = true;
        break;
      }
      if (AppendTranslatedRect(rect_value, x_offset, y_offset, &rects)) {
        ++appended_rect_count;
      }
    }
    if (rects.empty()) {
      continue;
    }

    base::Value::Dict output_item;
    output_item.Set("elementId", *element_id);
    output_item.Set("rects", std::move(rects));
    result_.items.Append(std::move(output_item));
    resolved_element_ids_.insert(*element_id);
  }
  result_.resolved_rect_count = appended_rect_count;
}

void AIPageElementRectResolver::StartTimer() {
  command_timeout_timer_.Stop();
  command_timeout_timer_.Start(
      FROM_HERE, kElementRectResolveTimeout,
      base::BindOnce(&AIPageElementRectResolver::OnTimeout,
                     base::WrapRefCounted(this)));
}

void AIPageElementRectResolver::OnTimeout() {
  CEF_REQUIRE_UIT();

  if (completed_) {
    return;
  }
  result_.timed_out = true;
  CompleteWithResult();
}

void AIPageElementRectResolver::CompleteWithResult() {
  BuildTopFrameItems();
  for (const std::string &element_id : unresolved_element_ids_) {
    if (result_.invalid_element_ids.find(element_id) ==
        result_.invalid_element_ids.end()) {
      result_.missing_element_ids.insert(element_id);
    }
  }

  if (!result_.truncated) {
    for (const std::string &element_id : requested_ids_) {
      if (resolved_element_ids_.find(element_id) ==
              resolved_element_ids_.end() &&
          result_.invalid_element_ids.find(element_id) ==
              result_.invalid_element_ids.end()) {
        result_.missing_element_ids.insert(element_id);
      }
    }
  }
  Complete();
}

void AIPageElementRectResolver::Complete() {
  if (completed_) {
    return;
  }
  completed_ = true;
  command_timeout_timer_.Stop();
  if (callback_) {
    std::move(callback_).Run(std::move(result_));
  }
}

} // namespace arkweb::ai_page
#endif // BUILDFLAG(IS_OHOS)
