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

#include "cef/ohos_cef_ext/libcef/browser/ai_page_annotation_request.h"

#include <algorithm>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "build/build_config.h"
#include "cef/libcef/browser/browser_info.h"
#include "cef/libcef/browser/frame_host_impl.h"
#include "cef/libcef/browser/thread_util.h"
#include "cef/ohos_cef_ext/libcef/browser/ai_page_element_rect_resolver.h"
#include "cef/ohos_cef_ext/libcef/browser/arkweb_frame_host_impl_ext.h"

#if BUILDFLAG(IS_OHOS)
namespace arkweb::ai_page {
namespace {

constexpr base::TimeDelta kPageAnnotationCommandTimeout =
    base::Milliseconds(5000);
constexpr int kDefaultPageAnnotationMaxMarks = 350;
constexpr int kMaxPageAnnotationMaxMarks = 1000;

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

std::string BuildPageAnnotationErrorJson(const char *error) {
  base::Value::Dict result;
  result.Set("ok", false);
  if (error && error[0] != '\0') {
    result.Set("error", error);
  }
  return ToJson(std::move(result));
}

base::Value::List BuildStringList(const std::set<std::string> &strings) {
  base::Value::List list;
  for (const std::string &string : strings) {
    list.Append(string);
  }
  return list;
}

void AddStringValues(const base::Value::List *values,
                     std::set<std::string> *output) {
  if (!values || !output) {
    return;
  }
  for (const base::Value &value : *values) {
    if (value.is_string() && !value.GetString().empty()) {
      output->insert(value.GetString());
    }
  }
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

int ReadPageAnnotationMaxMarks(const base::Value::Dict &params) {
  const base::Value::Dict *style = params.FindDict("style");
  if (!style) {
    return kDefaultPageAnnotationMaxMarks;
  }
  std::optional<double> max_marks = ReadNumberField(*style, "maxMarks");
  if (!max_marks || *max_marks <= 0) {
    return kDefaultPageAnnotationMaxMarks;
  }
  if (*max_marks >= kMaxPageAnnotationMaxMarks) {
    return kMaxPageAnnotationMaxMarks;
  }
  return static_cast<int>(*max_marks);
}

bool BuildPageAnnotationElementInput(const base::Value::Dict &params,
                                     std::vector<std::string> *element_ids) {
  const base::Value::List *element_values = params.FindList("elementList");
  if (!element_values || element_values->empty()) {
    return false;
  }

  for (const base::Value &value : *element_values) {
    if (!value.is_string() || value.GetString().empty()) {
      return false;
    }
    element_ids->push_back(value.GetString());
  }

  return true;
}

CefRefPtr<ArkwebFrameHostExtImpl>
GetTopFrame(scoped_refptr<CefBrowserInfo> browser_info,
            CefRefPtr<ArkwebFrameHostExtImpl> fallback_frame) {
  if (browser_info) {
    CefRefPtr<CefFrameHostImpl> main_frame = browser_info->GetMainFrame();
    if (main_frame && main_frame->frame_token() &&
        main_frame->GetRenderFrameHost() && main_frame->AsArkWebFrame()) {
      return static_cast<ArkwebFrameHostExtImpl *>(main_frame.get());
    }
  }
  if (fallback_frame && fallback_frame->frame_token() &&
      fallback_frame->GetRenderFrameHost()) {
    return fallback_frame;
  }
  return nullptr;
}

class PageAnnotationAddRequest
    : public base::RefCountedThreadSafe<PageAnnotationAddRequest> {
public:
  static void Run(scoped_refptr<CefBrowserInfo> browser_info,
                  CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                  std::string params,
                  CefRefPtr<CefAIPageCommandCallback> callback) {
    if (!CEF_CURRENTLY_ON_UIT()) {
      CEF_POST_TASK(CEF_UIT, base::BindOnce(&PageAnnotationAddRequest::Run,
                                            std::move(browser_info), root_frame,
                                            std::move(params), callback));
      return;
    }

    scoped_refptr<PageAnnotationAddRequest> request =
        new PageAnnotationAddRequest(std::move(browser_info), root_frame,
                                     std::move(params), callback);
    request->Start();
  }

private:
  friend class base::RefCountedThreadSafe<PageAnnotationAddRequest>;

  enum class Phase {
    kIdle,
    kClearing,
    kResolving,
    kRendering,
    kCompleted,
  };

  PageAnnotationAddRequest(scoped_refptr<CefBrowserInfo> browser_info,
                           CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                           std::string params,
                           CefRefPtr<CefAIPageCommandCallback> callback)
      : browser_info_(std::move(browser_info)), root_frame_(root_frame),
        params_(std::move(params)), callback_(callback) {}

  ~PageAnnotationAddRequest() = default;

  void Start() {
    CEF_REQUIRE_UIT();

    std::optional<base::Value::Dict> params_dict = ParseJsonDict(params_);
    if (!params_dict) {
      Complete(BuildPageAnnotationErrorJson("params must be a JSON object"));
      return;
    }

    params_dict_ = std::move(params_dict);
    if (!BuildPageAnnotationElementInput(*params_dict_, &element_ids_)) {
      Complete(BuildPageAnnotationErrorJson(
          "elementList must be a non-empty string array"));
      return;
    }

    top_frame_ = GetTopFrame(browser_info_, root_frame_);
    if (!top_frame_) {
      StartResolve();
      return;
    }
    StartClear();
  }

  void StartResolve() {
    CEF_REQUIRE_UIT();

    if (completed_) {
      return;
    }
    int max_rects = params_dict_ ? ReadPageAnnotationMaxMarks(*params_dict_)
                                 : kDefaultPageAnnotationMaxMarks;
    phase_ = Phase::kResolving;
    AIPageElementRectResolver::Resolve(
        browser_info_, root_frame_, std::move(element_ids_), max_rects,
        base::BindOnce(&PageAnnotationAddRequest::OnRectsResolved,
                       base::WrapRefCounted(this)));
  }

  void OnRectsResolved(AIPageElementRectResolveResult result) {
    CEF_REQUIRE_UIT();

    if (phase_ != Phase::kResolving || completed_) {
      return;
    }

    if (result.top_frame) {
      top_frame_ = result.top_frame;
    }
    resolved_items_ = std::move(result.items);
    missing_element_ids_ = std::move(result.missing_element_ids);
    invalid_element_ids_ = std::move(result.invalid_element_ids);
    truncated_ = result.truncated;
    timed_out_ = result.timed_out;

    if (!top_frame_) {
      CompleteWithResult();
      return;
    }
    StartRender();
  }

  void StartClear() {
    CEF_REQUIRE_UIT();

    phase_ = Phase::kClearing;
    pending_count_ = 1;
    StartTimer();
    top_frame_->RemovePageAnnotationOverlayWithResponse(base::BindOnce(
        &PageAnnotationAddRequest::OnFrameCleared, base::WrapRefCounted(this)));
  }

  void OnFrameCleared(const std::string &) {
    CEF_REQUIRE_UIT();

    if (phase_ != Phase::kClearing || completed_) {
      return;
    }
    DecrementPendingOrAdvance(base::BindOnce(
        &PageAnnotationAddRequest::StartResolve, base::WrapRefCounted(this)));
  }

  base::Value::List BuildRenderItems() {
    base::Value::List render_items;
    for (const base::Value &item_value : resolved_items_) {
      if (!item_value.is_dict()) {
        continue;
      }
      base::Value::Dict item = item_value.GetDict().Clone();
      const std::string *element_id = item.FindString("elementId");
      if (!element_id || element_id->empty()) {
        continue;
      }
      render_items.Append(std::move(item));
    }
    return render_items;
  }

  void StartRender() {
    CEF_REQUIRE_UIT();

    if (completed_) {
      return;
    }
    phase_ = Phase::kRendering;

    if (resolved_items_.empty()) {
      CompleteWithResult();
      return;
    }
    base::Value::List top_items = BuildRenderItems();
    if (top_items.empty()) {
      CompleteWithResult();
      return;
    }

    base::Value::Dict render_params;
    render_params.Set("items", std::move(top_items));
    if (params_dict_) {
      const base::Value *style = params_dict_->Find("style");
      if (style) {
        render_params.Set("style", style->Clone());
      }
    }

    pending_count_ = 1;
    StartTimer();
    top_frame_->RenderPageAnnotationOverlayWithResponse(
        ToJson(std::move(render_params)),
        base::BindOnce(&PageAnnotationAddRequest::OnOverlayRendered,
                       base::WrapRefCounted(this)));
  }

  void OnOverlayRendered(const std::string &result) {
    CEF_REQUIRE_UIT();

    if (phase_ != Phase::kRendering || completed_) {
      return;
    }

    std::optional<base::Value::Dict> result_dict = ParseJsonDict(result);
    if (result_dict) {
      rendered_count_ += result_dict->FindInt("renderedCount").value_or(0);
      truncated_ =
          truncated_ || result_dict->FindBool("truncated").value_or(false);
      AddStringValues(result_dict->FindList("missingElementIds"),
                      &missing_element_ids_);
    }

    DecrementPendingOrAdvance(
        base::BindOnce(&PageAnnotationAddRequest::CompleteWithResult,
                       base::WrapRefCounted(this)));
  }

  void OnTimeout() {
    CEF_REQUIRE_UIT();

    if (completed_) {
      return;
    }

    timed_out_ = true;
    if (phase_ == Phase::kClearing) {
      StartResolve();
      return;
    }
    if (phase_ == Phase::kRendering) {
      CompleteWithResult();
    }
  }

  void StartTimer() {
    command_timeout_timer_.Stop();
    command_timeout_timer_.Start(
        FROM_HERE, kPageAnnotationCommandTimeout,
        base::BindOnce(&PageAnnotationAddRequest::OnTimeout,
                       base::WrapRefCounted(this)));
  }

  void DecrementPendingOrAdvance(base::OnceClosure advance_callback) {
    if (pending_count_ > 0) {
      --pending_count_;
    }
    if (pending_count_ == 0) {
      command_timeout_timer_.Stop();
      std::move(advance_callback).Run();
    }
  }

  void CompleteWithResult() { Complete(BuildResultJson()); }

  std::string BuildResultJson() {
    base::Value::Dict result;
    result.Set("ok", !timed_out_);
    if (timed_out_) {
      result.Set("error", "timed out");
      return ToJson(std::move(result));
    }
    result.Set("renderedCount", rendered_count_);
    result.Set("truncated", truncated_);
    result.Set("missingElementIds", BuildStringList(missing_element_ids_));
    result.Set("invalidElementIds", BuildStringList(invalid_element_ids_));
    return ToJson(std::move(result));
  }

  void Complete(std::string result) {
    if (completed_) {
      return;
    }
    completed_ = true;
    phase_ = Phase::kCompleted;
    command_timeout_timer_.Stop();
    if (callback_) {
      callback_->OnResult(result);
    }
  }

  scoped_refptr<CefBrowserInfo> browser_info_;
  CefRefPtr<ArkwebFrameHostExtImpl> root_frame_;
  CefRefPtr<ArkwebFrameHostExtImpl> top_frame_;
  std::string params_;
  CefRefPtr<CefAIPageCommandCallback> callback_;
  std::optional<base::Value::Dict> params_dict_;
  std::vector<std::string> element_ids_;
  base::Value::List resolved_items_;
  std::set<std::string> missing_element_ids_;
  std::set<std::string> invalid_element_ids_;
  base::OneShotTimer command_timeout_timer_;
  Phase phase_ = Phase::kIdle;
  size_t pending_count_ = 0;
  int rendered_count_ = 0;
  bool truncated_ = false;
  bool timed_out_ = false;
  bool completed_ = false;
};

class PageAnnotationRemoveRequest
    : public base::RefCountedThreadSafe<PageAnnotationRemoveRequest> {
public:
  static void Run(scoped_refptr<CefBrowserInfo> browser_info,
                  CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                  CefRefPtr<CefAIPageCommandCallback> callback) {
    if (!CEF_CURRENTLY_ON_UIT()) {
      CEF_POST_TASK(CEF_UIT, base::BindOnce(&PageAnnotationRemoveRequest::Run,
                                            std::move(browser_info), root_frame,
                                            callback));
      return;
    }

    scoped_refptr<PageAnnotationRemoveRequest> request =
        new PageAnnotationRemoveRequest(std::move(browser_info), root_frame,
                                        callback);
    request->Start();
  }

private:
  friend class base::RefCountedThreadSafe<PageAnnotationRemoveRequest>;

  PageAnnotationRemoveRequest(scoped_refptr<CefBrowserInfo> browser_info,
                              CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                              CefRefPtr<CefAIPageCommandCallback> callback)
      : browser_info_(std::move(browser_info)), root_frame_(root_frame),
        callback_(callback) {}

  ~PageAnnotationRemoveRequest() = default;

  void Start() {
    CEF_REQUIRE_UIT();

    top_frame_ = GetTopFrame(browser_info_, root_frame_);
    if (!top_frame_) {
      Complete();
      return;
    }
    pending_count_ = 1;

    command_timeout_timer_.Start(
        FROM_HERE, kPageAnnotationCommandTimeout,
        base::BindOnce(&PageAnnotationRemoveRequest::OnTimeout,
                       base::WrapRefCounted(this)));
    top_frame_->RemovePageAnnotationOverlayWithResponse(
        base::BindOnce(&PageAnnotationRemoveRequest::OnOverlayRemoved,
                       base::WrapRefCounted(this)));
  }

  void OnOverlayRemoved(const std::string &result) {
    CEF_REQUIRE_UIT();

    if (completed_) {
      return;
    }

    std::optional<base::Value::Dict> result_dict = ParseJsonDict(result);
    if (result_dict && !result_dict->FindBool("ok").value_or(false)) {
      const std::string *error = result_dict->FindString("error");
      error_ = error && !error->empty() ? *error : "remove overlay failed";
    }

    if (pending_count_ > 0) {
      --pending_count_;
    }
    if (pending_count_ == 0) {
      Complete();
    }
  }

  void OnTimeout() {
    CEF_REQUIRE_UIT();

    if (completed_) {
      return;
    }
    timed_out_ = true;
    Complete();
  }

  void Complete() {
    if (completed_) {
      return;
    }
    completed_ = true;
    command_timeout_timer_.Stop();

    base::Value::Dict result;
    if (timed_out_) {
      result.Set("ok", false);
      result.Set("error", "timed out");
    } else if (!error_.empty()) {
      result.Set("ok", false);
      result.Set("error", error_);
    } else {
      result.Set("ok", true);
    }
    if (callback_) {
      callback_->OnResult(ToJson(std::move(result)));
    }
  }

  scoped_refptr<CefBrowserInfo> browser_info_;
  CefRefPtr<ArkwebFrameHostExtImpl> root_frame_;
  CefRefPtr<ArkwebFrameHostExtImpl> top_frame_;
  CefRefPtr<CefAIPageCommandCallback> callback_;
  std::string error_;
  base::OneShotTimer command_timeout_timer_;
  size_t pending_count_ = 0;
  bool timed_out_ = false;
  bool completed_ = false;
};

} // namespace

void RunAddPageAnnotation(scoped_refptr<CefBrowserInfo> browser_info,
                          CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                          std::string params,
                          CefRefPtr<CefAIPageCommandCallback> callback) {
  PageAnnotationAddRequest::Run(std::move(browser_info), root_frame,
                                std::move(params), callback);
}

void RunRemovePageAnnotation(scoped_refptr<CefBrowserInfo> browser_info,
                             CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                             CefRefPtr<CefAIPageCommandCallback> callback) {
  PageAnnotationRemoveRequest::Run(std::move(browser_info), root_frame,
                                   callback);
}

} // namespace arkweb::ai_page
#endif // BUILDFLAG(IS_OHOS)
