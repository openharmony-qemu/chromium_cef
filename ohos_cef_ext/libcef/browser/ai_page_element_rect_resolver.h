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

#ifndef CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_AI_PAGE_ELEMENT_RECT_RESOLVER_H_
#define CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_AI_PAGE_ELEMENT_RECT_RESOLVER_H_
#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/ref_counted.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "cef/include/cef_base.h"

class ArkwebFrameHostExtImpl;
class CefBrowserInfo;

namespace arkweb::ai_page {

struct AIPageElementRectResolveResult {
  AIPageElementRectResolveResult();
  AIPageElementRectResolveResult(AIPageElementRectResolveResult &&);
  AIPageElementRectResolveResult &operator=(AIPageElementRectResolveResult &&);
  AIPageElementRectResolveResult(const AIPageElementRectResolveResult &) =
      delete;
  AIPageElementRectResolveResult &
  operator=(const AIPageElementRectResolveResult &) = delete;
  ~AIPageElementRectResolveResult();

  CefRefPtr<ArkwebFrameHostExtImpl> top_frame;
  base::Value::List items;
  std::set<std::string> missing_element_ids;
  std::set<std::string> invalid_element_ids;
  int requested_count = 0;
  int resolved_rect_count = 0;
  bool truncated = false;
  bool timed_out = false;
};

using AIPageElementRectResolveCallback =
    base::OnceCallback<void(AIPageElementRectResolveResult)>;

class AIPageElementRectResolver
    : public base::RefCountedThreadSafe<AIPageElementRectResolver> {
public:
  static void Resolve(scoped_refptr<CefBrowserInfo> browser_info,
                      CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                      std::vector<std::string> element_ids, int max_rects,
                      AIPageElementRectResolveCallback callback);

  AIPageElementRectResolver(const AIPageElementRectResolver &) = delete;
  AIPageElementRectResolver &
  operator=(const AIPageElementRectResolver &) = delete;

private:
  friend class base::RefCountedThreadSafe<AIPageElementRectResolver>;

  struct ChildFrameOffset {
    std::string parent_frame_token;
    double left = 0;
    double top = 0;
  };

  AIPageElementRectResolver(scoped_refptr<CefBrowserInfo> browser_info,
                            CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                            std::vector<std::string> element_ids, int max_rects,
                            AIPageElementRectResolveCallback callback);
  ~AIPageElementRectResolver();

  void Start();
  void BuildElementRoutingState();
  void BuildFrameRoutingState();
  bool BuildFrameCaptureParams(const std::string &frame_token,
                               std::string *params) const;
  void StartCapture();
  void AddChildFrameOffsets(const std::string &parent_frame_token,
                            const base::Value::List *child_frames);
  void OnRectsCaptured(size_t frame_index, const std::string &result);
  bool ResolveFrameOffsetInternal(const std::string &frame_token,
                                  std::set<std::string> *visiting,
                                  double *x_offset, double *y_offset) const;
  bool ResolveFrameOffset(const std::string &frame_token, double *x_offset,
                          double *y_offset) const;
  void BuildTopFrameItems();
  void StartTimer();
  void OnTimeout();
  void CompleteWithResult();
  void Complete();

  scoped_refptr<CefBrowserInfo> browser_info_;
  CefRefPtr<ArkwebFrameHostExtImpl> root_frame_;
  CefRefPtr<ArkwebFrameHostExtImpl> top_frame_;
  std::string top_frame_token_;
  std::vector<std::string> element_ids_;
  int max_rects_ = 1;
  AIPageElementRectResolveCallback callback_;
  std::vector<CefRefPtr<ArkwebFrameHostExtImpl>> frames_;
  std::map<std::string, std::string> frame_parent_tokens_;
  std::set<std::string> frame_tree_probe_tokens_;
  std::set<std::string> target_frame_tokens_;
  std::map<std::string, base::Value::List> elements_by_frame_token_;
  base::Value::List captured_items_;
  std::map<std::string, ChildFrameOffset> child_frame_offsets_;
  std::set<std::string> requested_ids_;
  std::set<std::string> resolved_element_ids_;
  std::set<std::string> unresolved_element_ids_;
  base::OneShotTimer command_timeout_timer_;
  AIPageElementRectResolveResult result_;
  size_t pending_count_ = 0;
  bool completed_ = false;
};

} // namespace arkweb::ai_page

#endif // CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_AI_PAGE_ELEMENT_RECT_RESOLVER_H_
