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

#ifndef CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_AI_PAGE_DOM_FRAME_TREE_ASSEMBLER_H_
#define CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_AI_PAGE_DOM_FRAME_TREE_ASSEMBLER_H_
#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "cef/include/cef_base.h"
#include "content/public/browser/global_routing_id.h"
#include "third_party/blink/public/common/tokens/tokens.h"

class ArkwebFrameHostExtImpl;
class CefBrowserInfo;
class CefFrameHostImpl;

namespace arkweb::ai_page {

class AIPageDomFrameTreeAssembler
    : public base::RefCountedThreadSafe<AIPageDomFrameTreeAssembler> {
 public:
  using ResultCallback = base::OnceCallback<void(std::string)>;

  enum class Method {
    kFullDom,
    kLiteDom,
  };

  static void Capture(scoped_refptr<CefBrowserInfo> browser_info,
                      CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                      Method method,
                      const std::string& params,
                      ResultCallback callback);

  AIPageDomFrameTreeAssembler(const AIPageDomFrameTreeAssembler&) = delete;
  AIPageDomFrameTreeAssembler& operator=(
      const AIPageDomFrameTreeAssembler&) = delete;

 private:
  friend class base::RefCountedThreadSafe<AIPageDomFrameTreeAssembler>;

  AIPageDomFrameTreeAssembler(scoped_refptr<CefBrowserInfo> browser_info,
                              CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                              Method method,
                              std::string params,
                              ResultCallback callback);
  ~AIPageDomFrameTreeAssembler();

  void Start();
  void AddFrame(CefRefPtr<CefFrameHostImpl> frame);
  void OnFrameCaptured(content::GlobalRenderFrameHostToken frame_token,
                       const std::string& result);
  void OnCaptureTimeout();
  void Finish();
  void Complete(std::string result);
  bool PrepareFrameResult(
      const content::GlobalRenderFrameHostToken& frame_token,
      base::Value::Dict* result);
  void AddDiagnostic(
      const content::GlobalRenderFrameHostToken& frame_token,
      const std::string& message);

  std::optional<content::GlobalRenderFrameHostToken> ResolveChildFrameToken(
      const content::GlobalRenderFrameHostToken& owner_frame_token,
      const std::string& serialized_child_frame_token) const;
  void AssembleFullFrame(
      const content::GlobalRenderFrameHostToken& frame_token,
      base::Value::Dict* frame_result,
      std::set<content::GlobalRenderFrameHostToken>* active_frames);
  void AssembleFullNodeList(
      const content::GlobalRenderFrameHostToken& owner_frame_token,
      base::Value::List* nodes,
      std::set<content::GlobalRenderFrameHostToken>* active_frames);
  void AppendLiteFrame(
      const content::GlobalRenderFrameHostToken& frame_token,
      base::Value::List* output_nodes,
      std::set<content::GlobalRenderFrameHostToken>* active_frames);

  scoped_refptr<CefBrowserInfo> browser_info_;
  CefRefPtr<ArkwebFrameHostExtImpl> root_frame_;
  Method method_;
  std::string params_;
  ResultCallback callback_;
  std::optional<content::GlobalRenderFrameHostToken> root_frame_token_;
  std::map<content::GlobalRenderFrameHostToken, base::Value::Dict>
      frame_results_;
  std::map<content::GlobalRenderFrameHostToken, std::vector<std::string>>
      diagnostics_;
  std::map<blink::LocalFrameToken, content::GlobalRenderFrameHostToken>
      local_frame_tokens_;
  std::set<content::GlobalRenderFrameHostToken> requested_frames_;
  std::set<content::GlobalRenderFrameHostToken> pending_frames_;
  base::OneShotTimer capture_timeout_timer_;
  bool completed_ = false;
};

}  // namespace arkweb::ai_page

#endif  // CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_AI_PAGE_DOM_FRAME_TREE_ASSEMBLER_H_
