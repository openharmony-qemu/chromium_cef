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

#include "cef/ohos_cef_ext/libcef/browser/ai_page_dom_frame_tree_assembler.h"

#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "base/token.h"
#include "base/unguessable_token.h"
#include "build/build_config.h"
#include "cef/libcef/browser/browser_info.h"
#include "cef/libcef/browser/frame_host_impl.h"
#include "cef/libcef/browser/thread_util.h"
#include "cef/ohos_cef_ext/libcef/browser/arkweb_frame_host_impl_ext.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"

#if BUILDFLAG(IS_OHOS)
namespace arkweb::ai_page {
namespace {

constexpr char kInternalChildFrameTokenKey[] = "__arkweb_child_frame_token";
constexpr char kInternalChildFramesKey[] = "__arkweb_child_frames";
constexpr char kInternalDocumentScopeTokenKey[] =
    "__arkweb_document_scope_token";
constexpr char kInternalDomNodeIdKey[] = "__arkweb_dom_node_id";
constexpr char kNodeIdKey[] = "id";
// Keep this longer than the renderer-side lifecycle cleanup guard. Heavy pages
// may need the renderer fallback to fire and still have time to serialize the
// result before the browser gives up on the frame tree.
constexpr base::TimeDelta kAIPageCommandCaptureTimeout =
    base::Milliseconds(5000);

std::optional<base::UnguessableToken> ParseTokenValue(
    std::string_view value) {
  std::optional<base::Token> token =
      base::Token::FromString(std::string(value));
  if (!token) {
    return std::nullopt;
  }
  return base::UnguessableToken::Deserialize(token->high(), token->low());
}

std::optional<blink::FrameToken> ParseFrameToken(
    const std::string& serialized_token) {
  static constexpr std::string_view kLocalPrefix = "local:";
  static constexpr std::string_view kRemotePrefix = "remote:";

  if (base::StartsWith(serialized_token, kLocalPrefix,
                       base::CompareCase::SENSITIVE)) {
    std::optional<base::UnguessableToken> token =
        ParseTokenValue(std::string_view(serialized_token).substr(
            kLocalPrefix.size()));
    if (!token) {
      return std::nullopt;
    }
    return blink::FrameToken(blink::LocalFrameToken(*token));
  }

  if (base::StartsWith(serialized_token, kRemotePrefix,
                       base::CompareCase::SENSITIVE)) {
    std::optional<base::UnguessableToken> token =
        ParseTokenValue(std::string_view(serialized_token).substr(
            kRemotePrefix.size()));
    if (!token) {
      return std::nullopt;
    }
    return blink::FrameToken(blink::RemoteFrameToken(*token));
  }

  return std::nullopt;
}

std::optional<base::Value::Dict> ParseResultDict(const std::string& result) {
  std::optional<base::Value> value =
      base::JSONReader::Read(result, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!value || !value->is_dict()) {
    return std::nullopt;
  }
  return std::move(value->GetDict());
}

std::vector<std::string> GetInternalChildFrameTokens(
    const base::Value::Dict& frame_result) {
  std::vector<std::string> tokens;
  const base::Value::List* token_list =
      frame_result.FindList(kInternalChildFramesKey);
  if (!token_list) {
    return tokens;
  }
  for (const base::Value& token : *token_list) {
    if (token.is_string()) {
      tokens.push_back(token.GetString());
    }
  }
  return tokens;
}

void StripInternalFields(base::Value* value) {
  if (value->is_dict()) {
    base::Value::Dict& dict = value->GetDict();
    dict.Remove(kInternalChildFrameTokenKey);
    dict.Remove(kInternalChildFramesKey);
    dict.Remove(kInternalDocumentScopeTokenKey);
    dict.Remove(kInternalDomNodeIdKey);
    for (auto it = dict.begin(); it != dict.end(); ++it) {
      StripInternalFields(&it->second);
    }
    return;
  }

  if (!value->is_list()) {
    return;
  }
  for (base::Value& item : value->GetList()) {
    StripInternalFields(&item);
  }
}

void StripTopLevelInternalFields(base::Value::Dict* dict) {
  dict->Remove(kInternalChildFrameTokenKey);
  dict->Remove(kInternalChildFramesKey);
  dict->Remove(kInternalDocumentScopeTokenKey);
  dict->Remove(kInternalDomNodeIdKey);
}

void StripInternalDomNodeIds(base::Value* value) {
  if (value->is_dict()) {
    base::Value::Dict& dict = value->GetDict();
    dict.Remove(kInternalDomNodeIdKey);
    for (auto it = dict.begin(); it != dict.end(); ++it) {
      StripInternalDomNodeIds(&it->second);
    }
    return;
  }

  if (!value->is_list()) {
    return;
  }
  for (base::Value& item : value->GetList()) {
    StripInternalDomNodeIds(&item);
  }
}

void StripInternalDomNodeIds(base::Value::Dict* dict) {
  dict->Remove(kInternalDomNodeIdKey);
  for (auto it = dict->begin(); it != dict->end(); ++it) {
    StripInternalDomNodeIds(&it->second);
  }
}

bool HasChildFrames(const base::Value::Dict& frame_result) {
  const base::Value::List* child_frames =
      frame_result.FindList(kInternalChildFramesKey);
  return child_frames && !child_frames->empty();
}

std::string BuildElementIdPayload(
    const blink::LocalFrameToken& frame_token,
    const base::UnguessableToken& document_scope_token,
    int dom_node_id) {
  return base::StringPrintf("%s|%s|%d", frame_token.ToString().c_str(),
                            document_scope_token.ToString().c_str(),
                            dom_node_id);
}

std::string EncodeElementId(
    const content::GlobalRenderFrameHostToken& frame_token,
    const base::UnguessableToken& document_scope_token,
    int dom_node_id) {
  if (dom_node_id <= 0) {
    return std::string();
  }

  return BuildElementIdPayload(frame_token.frame_token, document_scope_token,
                               dom_node_id);
}

void EncodeNodeIds(base::Value* value,
                   const content::GlobalRenderFrameHostToken& frame_token,
                   const base::UnguessableToken& document_scope_token);

void EncodeNodeIds(base::Value::Dict* dict,
                   const content::GlobalRenderFrameHostToken& frame_token,
                   const base::UnguessableToken& document_scope_token) {
  base::Value* raw_id = dict->Find(kInternalDomNodeIdKey);
  if (raw_id && raw_id->is_int()) {
    std::string element_id =
        EncodeElementId(frame_token, document_scope_token, raw_id->GetInt());
    if (!element_id.empty()) {
      dict->Set(kNodeIdKey, std::move(element_id));
    }
  }
  dict->Remove(kInternalDomNodeIdKey);
  for (auto it = dict->begin(); it != dict->end(); ++it) {
    EncodeNodeIds(&it->second, frame_token, document_scope_token);
  }
}

void EncodeNodeIds(base::Value* value,
                   const content::GlobalRenderFrameHostToken& frame_token,
                   const base::UnguessableToken& document_scope_token) {
  if (value->is_dict()) {
    EncodeNodeIds(&value->GetDict(), frame_token, document_scope_token);
    return;
  }

  if (!value->is_list()) {
    return;
  }
  for (base::Value& item : value->GetList()) {
    EncodeNodeIds(&item, frame_token, document_scope_token);
  }
}

}  // namespace

// static
void AIPageDomFrameTreeAssembler::Capture(
    scoped_refptr<CefBrowserInfo> browser_info,
    CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
    Method method,
    const std::string& params,
    ResultCallback callback) {
  if (!CEF_CURRENTLY_ON_UIT()) {
    CEF_POST_TASK(CEF_UIT,
                  base::BindOnce(&AIPageDomFrameTreeAssembler::Capture,
                                 std::move(browser_info), root_frame, method,
                                 params, std::move(callback)));
    return;
  }

  scoped_refptr<AIPageDomFrameTreeAssembler> assembler =
      new AIPageDomFrameTreeAssembler(std::move(browser_info), root_frame,
                                      method, params, std::move(callback));
  assembler->Start();
}

AIPageDomFrameTreeAssembler::AIPageDomFrameTreeAssembler(
    scoped_refptr<CefBrowserInfo> browser_info,
    CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
    Method method,
    std::string params,
    ResultCallback callback)
    : browser_info_(std::move(browser_info)),
      root_frame_(root_frame),
      method_(method),
      params_(std::move(params)),
      callback_(std::move(callback)) {}

AIPageDomFrameTreeAssembler::~AIPageDomFrameTreeAssembler() = default;

void AIPageDomFrameTreeAssembler::Start() {
  CEF_REQUIRE_UIT();

  if (!browser_info_ || !root_frame_ || !root_frame_->frame_token()) {
    Complete(std::string());
    return;
  }

  root_frame_token_ = *root_frame_->frame_token();
  AddFrame(CefRefPtr<CefFrameHostImpl>(root_frame_.get()));
  for (const auto& frame : browser_info_->GetAllFrames()) {
    AddFrame(frame);
  }

  if (pending_frames_.empty()) {
    Complete(std::string());
    return;
  }

  capture_timeout_timer_.Start(
      FROM_HERE, kAIPageCommandCaptureTimeout,
      base::BindOnce(&AIPageDomFrameTreeAssembler::OnCaptureTimeout,
                     base::WrapRefCounted(this)));
}

void AIPageDomFrameTreeAssembler::AddFrame(CefRefPtr<CefFrameHostImpl> frame) {
  CEF_REQUIRE_UIT();

  if (!frame || !frame->frame_token()) {
    return;
  }

  content::GlobalRenderFrameHostToken frame_token = *frame->frame_token();
  if (requested_frames_.find(frame_token) != requested_frames_.end()) {
    return;
  }

  if (!frame->GetRenderFrameHost()) {
    AddDiagnostic(frame_token, "skip detached frame");
    return;
  }

  CefRefPtr<ArkwebFrameExt> arkweb_frame = frame->AsArkWebFrame();
  if (!arkweb_frame) {
    return;
  }

  requested_frames_.insert(frame_token);
  local_frame_tokens_[frame_token.frame_token] = frame_token;
  pending_frames_.insert(frame_token);

  auto* frame_ext = static_cast<ArkwebFrameHostExtImpl*>(frame.get());
  auto response_callback = base::BindOnce(
      &AIPageDomFrameTreeAssembler::OnFrameCaptured, base::WrapRefCounted(this),
      frame_token);
  if (method_ == Method::kFullDom) {
    frame_ext->GetFullDomWithResponse(params_, std::move(response_callback));
  } else {
    frame_ext->GetLiteDomWithResponse(params_, std::move(response_callback));
  }
}

void AIPageDomFrameTreeAssembler::OnFrameCaptured(
    content::GlobalRenderFrameHostToken frame_token,
    const std::string& result) {
  CEF_REQUIRE_UIT();

  if (completed_) {
    return;
  }

  std::optional<base::Value::Dict> result_dict = ParseResultDict(result);
  if (result_dict) {
    if (PrepareFrameResult(frame_token, &*result_dict)) {
      frame_results_[frame_token] = std::move(*result_dict);
    }
  } else {
    AddDiagnostic(frame_token, "invalid frame result");
  }

  pending_frames_.erase(frame_token);
  if (pending_frames_.empty()) {
    Finish();
  }
}

void AIPageDomFrameTreeAssembler::OnCaptureTimeout() {
  CEF_REQUIRE_UIT();

  if (completed_) {
    return;
  }

  for (const auto& frame_token : pending_frames_) {
    AddDiagnostic(frame_token, "frame capture timeout");
  }
  pending_frames_.clear();
  Finish();
}

bool AIPageDomFrameTreeAssembler::PrepareFrameResult(
    const content::GlobalRenderFrameHostToken& frame_token,
    base::Value::Dict* result) {
  const std::string* serialized_document_scope_token =
      result->FindString(kInternalDocumentScopeTokenKey);
  std::optional<base::UnguessableToken> result_document_scope_token =
      serialized_document_scope_token
          ? ParseTokenValue(*serialized_document_scope_token)
          : std::nullopt;

  content::RenderFrameHost* render_frame_host =
      content::RenderFrameHost::FromFrameToken(frame_token);
  std::optional<base::UnguessableToken> expected_document_scope_token =
      render_frame_host ? render_frame_host->GetEmbeddingToken()
                        : std::nullopt;

  if (!result_document_scope_token && !expected_document_scope_token) {
    StripInternalDomNodeIds(result);
    return true;
  }

  if (!result_document_scope_token || !expected_document_scope_token ||
      !(*result_document_scope_token == *expected_document_scope_token)) {
    AddDiagnostic(frame_token, "document scope token mismatch");
    return false;
  }

  EncodeNodeIds(result, frame_token, *result_document_scope_token);
  return true;
}

std::optional<content::GlobalRenderFrameHostToken>
AIPageDomFrameTreeAssembler::ResolveChildFrameToken(
    const content::GlobalRenderFrameHostToken& owner_frame_token,
    const std::string& serialized_child_frame_token) const {
  std::optional<blink::FrameToken> child_frame_token =
      ParseFrameToken(serialized_child_frame_token);
  if (!child_frame_token) {
    return std::nullopt;
  }

  if (child_frame_token->Is<blink::LocalFrameToken>()) {
    blink::LocalFrameToken local_token =
        child_frame_token->GetAs<blink::LocalFrameToken>();
    auto known_frame = local_frame_tokens_.find(local_token);
    if (known_frame != local_frame_tokens_.end()) {
      return known_frame->second;
    }

    content::GlobalRenderFrameHostToken same_process_token(
        owner_frame_token.child_id, local_token);
    if (browser_info_ &&
        browser_info_->GetFrameForGlobalToken(same_process_token)) {
      return same_process_token;
    }
    return std::nullopt;
  }

  content::RenderFrameHost* owner_frame =
      content::RenderFrameHost::FromFrameToken(owner_frame_token);
  if (!owner_frame || !owner_frame->GetProcess()) {
    return std::nullopt;
  }

  content::RenderFrameHost* child_frame =
      content::RenderFrameHost::FromPlaceholderToken(
          owner_frame->GetProcess()->GetID().GetUnsafeValue(),
          child_frame_token->GetAs<blink::RemoteFrameToken>());
  if (!child_frame) {
    return std::nullopt;
  }
  return child_frame->GetGlobalFrameToken();
}

void AIPageDomFrameTreeAssembler::AssembleFullFrame(
    const content::GlobalRenderFrameHostToken& frame_token,
    base::Value::Dict* frame_result,
    std::set<content::GlobalRenderFrameHostToken>* active_frames) {
  frame_result->Remove(kInternalChildFramesKey);
  AssembleFullNodeList(frame_token, frame_result->FindList("children_nodes"),
                       active_frames);
}

void AIPageDomFrameTreeAssembler::AssembleFullNodeList(
    const content::GlobalRenderFrameHostToken& owner_frame_token,
    base::Value::List* nodes,
    std::set<content::GlobalRenderFrameHostToken>* active_frames) {
  if (!nodes) {
    return;
  }

  for (base::Value& value : *nodes) {
    if (!value.is_dict()) {
      continue;
    }

    base::Value::Dict& node = value.GetDict();
    const std::string* child_token =
        node.FindString(kInternalChildFrameTokenKey);
    std::string child_token_value = child_token ? *child_token : std::string();
    node.Remove(kInternalChildFrameTokenKey);

    if (!child_token_value.empty()) {
      std::optional<content::GlobalRenderFrameHostToken> child_frame_token =
          ResolveChildFrameToken(owner_frame_token, child_token_value);
      auto child_result =
          child_frame_token ? frame_results_.find(*child_frame_token)
                            : frame_results_.end();
      if (child_frame_token && child_result != frame_results_.end() &&
          active_frames->find(*child_frame_token) == active_frames->end()) {
        active_frames->insert(*child_frame_token);
        AssembleFullFrame(*child_frame_token, &child_result->second,
                          active_frames);
        active_frames->erase(*child_frame_token);

        base::Value::List stitched_children;
        if (base::Value::List* child_roots =
                child_result->second.FindList("children_nodes")) {
          for (base::Value& child_root : *child_roots) {
            stitched_children.Append(std::move(child_root));
          }
        }
        node.Set("children_nodes", std::move(stitched_children));
        continue;
      }

      AddDiagnostic(owner_frame_token, "unresolved child frame");
    }

    AssembleFullNodeList(owner_frame_token, node.FindList("children_nodes"),
                         active_frames);
  }
}

void AIPageDomFrameTreeAssembler::AppendLiteFrame(
    const content::GlobalRenderFrameHostToken& frame_token,
    base::Value::List* output_nodes,
    std::set<content::GlobalRenderFrameHostToken>* active_frames) {
  auto frame_result = frame_results_.find(frame_token);
  if (frame_result == frame_results_.end()) {
    return;
  }

  if (active_frames->find(frame_token) != active_frames->end()) {
    AddDiagnostic(frame_token, "detected frame cycle");
    return;
  }
  active_frames->insert(frame_token);

  std::vector<std::string> child_tokens =
      GetInternalChildFrameTokens(frame_result->second);

  if (base::Value::List* nodes = frame_result->second.FindList("nodes")) {
    for (base::Value& node : *nodes) {
      StripInternalFields(&node);
      output_nodes->Append(std::move(node));
    }
  }

  for (const std::string& child_token : child_tokens) {
    std::optional<content::GlobalRenderFrameHostToken> child_frame_token =
        ResolveChildFrameToken(frame_token, child_token);
    if (!child_frame_token) {
      AddDiagnostic(frame_token, "unresolved lite child frame");
      continue;
    }
    AppendLiteFrame(*child_frame_token, output_nodes, active_frames);
  }

  frame_result->second.Remove(kInternalChildFramesKey);
  active_frames->erase(frame_token);
}

void AIPageDomFrameTreeAssembler::Finish() {
  CEF_REQUIRE_UIT();

  if (!root_frame_token_) {
    Complete(std::string());
    return;
  }

  auto root_result = frame_results_.find(*root_frame_token_);
  if (root_result == frame_results_.end()) {
    Complete(std::string());
    return;
  }

  if (method_ == Method::kFullDom) {
    if (!HasChildFrames(root_result->second)) {
      StripTopLevelInternalFields(&root_result->second);
      base::Value root_value(std::move(root_result->second));
      std::string json;
      base::JSONWriter::Write(root_value, &json);
      Complete(std::move(json));
      return;
    }

    std::set<content::GlobalRenderFrameHostToken> active_frames = {
        *root_frame_token_};
    AssembleFullFrame(*root_frame_token_, &root_result->second, &active_frames);

    StripTopLevelInternalFields(&root_result->second);
    base::Value root_value(std::move(root_result->second));
    std::string json;
    base::JSONWriter::Write(root_value, &json);
    Complete(std::move(json));
    return;
  }

  if (!HasChildFrames(root_result->second)) {
    StripTopLevelInternalFields(&root_result->second);
    base::Value root_value(std::move(root_result->second));
    std::string json;
    base::JSONWriter::Write(root_value, &json);
    Complete(std::move(json));
    return;
  }

  base::Value::List merged_nodes;
  std::set<content::GlobalRenderFrameHostToken> active_frames;
  AppendLiteFrame(*root_frame_token_, &merged_nodes, &active_frames);

  StripTopLevelInternalFields(&root_result->second);
  root_result->second.Set("nodes", std::move(merged_nodes));

  base::Value root_value(std::move(root_result->second));
  std::string json;
  base::JSONWriter::Write(root_value, &json);
  Complete(std::move(json));
}

void AIPageDomFrameTreeAssembler::Complete(std::string result) {
  if (completed_) {
    return;
  }
  completed_ = true;
  capture_timeout_timer_.Stop();
  if (callback_) {
    std::move(callback_).Run(std::move(result));
  }
}

void AIPageDomFrameTreeAssembler::AddDiagnostic(
    const content::GlobalRenderFrameHostToken& frame_token,
    const std::string& message) {
  diagnostics_[frame_token].push_back(message);
  LOG(DEBUG) << "AIPageDomFrameTreeAssembler [privacy] " << message;
}

}  // namespace arkweb::ai_page
#endif  // BUILDFLAG(IS_OHOS)
