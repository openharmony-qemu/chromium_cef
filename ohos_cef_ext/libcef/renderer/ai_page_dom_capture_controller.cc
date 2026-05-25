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

#include "cef/ohos_cef_ext/libcef/renderer/ai_page_dom_capture_controller.h"

#if BUILDFLAG(IS_OHOS)

#include <utility>

#include "base/time/time.h"
#include "cef/ohos_cef_ext/libcef/renderer/ai_page_dom_collector.h"
#include "third_party/blink/public/platform/task_type.h"
#include "third_party/blink/public/web/web_frame.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/frame.h"
#include "third_party/blink/renderer/core/frame/local_frame.h"
#include "third_party/blink/renderer/core/frame/local_frame_view.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/platform/wtf/casting.h"

namespace arkweb::ai_page {
namespace {

// Renderer-side lifecycle guard. Keep this shorter than the browser-side frame
// tree timeout so the fallback capture can still complete and return a result.
constexpr base::TimeDelta kAIPageCommandCaptureTimeout =
    base::Milliseconds(2000);

}  // namespace

// static
void AIPageDomCaptureController::Capture(blink::WebLocalFrame* web_frame,
                                         Method method,
                                         std::string params,
                                         ResultCallback callback) {
  if (!web_frame) {
    std::move(callback).Run(std::string());
    return;
  }

  blink::Frame* core_frame = blink::WebFrame::ToCoreFrame(*web_frame);
  blink::LocalFrame* local_frame = blink::DynamicTo<blink::LocalFrame>(
      core_frame);
  if (!local_frame || !local_frame->GetDocument()) {
    std::move(callback).Run(std::string());
    return;
  }

  AIPageDomCaptureController* controller =
      blink::MakeGarbageCollected<AIPageDomCaptureController>(
          base::PassKey<AIPageDomCaptureController>(), *local_frame);
  controller->Capture(method, std::move(params), std::move(callback));
}

AIPageDomCaptureController::AIPageDomCaptureController(
    base::PassKey<AIPageDomCaptureController>,
    blink::LocalFrame& frame)
    : frame_(&frame),
      cleanup_timer_(frame.GetTaskRunner(blink::TaskType::kInternalDefault),
                     this,
                     &AIPageDomCaptureController::OnCleanupTimer) {}

AIPageDomCaptureController::~AIPageDomCaptureController() {
  ClearPendingCaptures();
}

AIPageDomCaptureController::PendingCapture::PendingCapture(
    Method method,
    std::string params,
    ResultCallback callback)
    : method(method),
      params(std::move(params)),
      callback(std::move(callback)) {}

AIPageDomCaptureController::PendingCapture::PendingCapture(PendingCapture&&) =
    default;

AIPageDomCaptureController::PendingCapture&
AIPageDomCaptureController::PendingCapture::operator=(PendingCapture&&) =
    default;

AIPageDomCaptureController::PendingCapture::~PendingCapture() = default;

void AIPageDomCaptureController::Capture(Method method,
                                         std::string params,
                                         ResultCallback callback) {
  blink::LocalFrameView* view = frame_ ? frame_->View() : nullptr;
  const bool is_main_frame = frame_ && frame_->IsMainFrame();
  if (!is_main_frame || !view || !view->LifecycleUpdatePending()) {
    CaptureSync(method, params, std::move(callback));
    return;
  }

  EnsureLifecycleObserverRegistered();
  pending_captures_.emplace_back(method, std::move(params),
                                 std::move(callback));
  keep_alive_ = this;
  cleanup_timer_.StartOneShot(kAIPageCommandCaptureTimeout, FROM_HERE);
  view->ScheduleAnimation();
}

void AIPageDomCaptureController::CaptureSync(Method method,
                                             const std::string& params,
                                             ResultCallback callback) {
  if (!frame_) {
    std::move(callback).Run(std::string());
    return;
  }

  blink::WebLocalFrame* web_frame =
      blink::WebLocalFrameImpl::FromFrame(frame_.Get());
  if (!web_frame) {
    std::move(callback).Run(std::string());
    return;
  }

  std::move(callback).Run(method == Method::kFullDom
                              ? BuildFullDomResult(web_frame, params)
                              : BuildLiteDomResult(web_frame, params));
}

void AIPageDomCaptureController::EnsureLifecycleObserverRegistered() {
  if (is_lifecycle_observer_registered_) {
    return;
  }

  blink::LocalFrameView* view = frame_ ? frame_->View() : nullptr;
  if (!view) {
    return;
  }

  view->RegisterForLifecycleNotifications(this);
  is_lifecycle_observer_registered_ = true;
}

void AIPageDomCaptureController::DidFinishPostLifecycleSteps(
    const blink::LocalFrameView&) {
  RunPendingCaptures();
}

void AIPageDomCaptureController::RunPendingCaptures() {
  std::vector<PendingCapture> pending_captures;
  pending_captures.swap(pending_captures_);
  cleanup_timer_.Stop();
  if (is_lifecycle_observer_registered_) {
    blink::LocalFrameView* view = frame_ ? frame_->View() : nullptr;
    if (view) {
      view->UnregisterFromLifecycleNotifications(this);
    }
    is_lifecycle_observer_registered_ = false;
  }
  for (auto& capture : pending_captures) {
    CaptureSync(capture.method, capture.params, std::move(capture.callback));
  }
  keep_alive_.Clear();
}

void AIPageDomCaptureController::ClearPendingCaptures() {
  pending_captures_.clear();
  cleanup_timer_.Stop();
  if (is_lifecycle_observer_registered_) {
    blink::LocalFrameView* view = frame_ ? frame_->View() : nullptr;
    if (view) {
      view->UnregisterFromLifecycleNotifications(this);
    }
    is_lifecycle_observer_registered_ = false;
  }
  keep_alive_.Clear();
}

void AIPageDomCaptureController::OnCleanupTimer(blink::TimerBase*) {
  RunPendingCaptures();
}

void AIPageDomCaptureController::Trace(blink::Visitor* visitor) const {
  visitor->Trace(frame_);
  visitor->Trace(cleanup_timer_);
}

}  // namespace arkweb::ai_page

#endif  // BUILDFLAG(IS_OHOS)
