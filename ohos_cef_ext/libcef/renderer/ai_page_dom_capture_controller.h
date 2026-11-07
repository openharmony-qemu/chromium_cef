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

#ifndef CEF_OHOS_CEF_EXT_LIBCEF_RENDERER_AI_PAGE_DOM_CAPTURE_CONTROLLER_H_
#define CEF_OHOS_CEF_EXT_LIBCEF_RENDERER_AI_PAGE_DOM_CAPTURE_CONTROLLER_H_
#pragma once

#include "build/build_config.h"

#if BUILDFLAG(IS_OHOS)

#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/types/pass_key.h"

#ifndef INSIDE_BLINK
#define INSIDE_BLINK 1
#endif

#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/platform/heap/garbage_collected.h"
#include "third_party/blink/renderer/platform/heap/member.h"
#include "third_party/blink/renderer/platform/heap/self_keep_alive.h"
#include "third_party/blink/renderer/platform/timer.h"

namespace blink {
class LocalFrame;
class WebLocalFrame;
}  // namespace blink

namespace arkweb::ai_page {

// Defers AI page DOM collection until Blink has completed pending lifecycle
// work, following the 144 APC/DomSnapshotter capture timing.
class AIPageDomCaptureController final
    : public blink::GarbageCollected<AIPageDomCaptureController>,
      public blink::LocalFrameView::LifecycleNotificationObserver {
 public:
  enum class Method {
    kFullDom,
    kLiteDom,
  };

  using ResultCallback = base::OnceCallback<void(std::string)>;

  static void Capture(blink::WebLocalFrame* frame,
                      Method method,
                      std::string params,
                      ResultCallback callback);

  explicit AIPageDomCaptureController(
      base::PassKey<AIPageDomCaptureController>,
      blink::LocalFrame& frame);
  AIPageDomCaptureController(const AIPageDomCaptureController&) = delete;
  AIPageDomCaptureController& operator=(const AIPageDomCaptureController&) =
      delete;
  ~AIPageDomCaptureController();

  void DidFinishPostLifecycleSteps(const blink::LocalFrameView&) override;
  void Trace(blink::Visitor* visitor) const override;

 private:
  struct PendingCapture {
    PendingCapture(Method method,
                   std::string params,
                   ResultCallback callback);
    PendingCapture(PendingCapture&&);
    PendingCapture& operator=(PendingCapture&&);
    ~PendingCapture();

    Method method;
    std::string params;
    ResultCallback callback;
  };

  void Capture(Method method, std::string params, ResultCallback callback);
  void CaptureSync(Method method,
                   const std::string& params,
                   ResultCallback callback);
  void EnsureLifecycleObserverRegistered();
  void RunPendingCaptures();
  void ClearPendingCaptures();
  void OnCleanupTimer(blink::TimerBase*);

  blink::WeakMember<blink::LocalFrame> frame_;
  blink::HeapTaskRunnerTimer<AIPageDomCaptureController> cleanup_timer_;
  blink::SelfKeepAlive<AIPageDomCaptureController> keep_alive_;
  bool is_lifecycle_observer_registered_ = false;
  std::vector<PendingCapture> pending_captures_;
};

}  // namespace arkweb::ai_page

#endif  // BUILDFLAG(IS_OHOS)

#endif  // CEF_OHOS_CEF_EXT_LIBCEF_RENDERER_AI_PAGE_DOM_CAPTURE_CONTROLLER_H_
