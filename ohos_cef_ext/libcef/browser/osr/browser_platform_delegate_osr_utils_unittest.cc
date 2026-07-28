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

#include "arkweb/build/features/features.h"
#include "base/test/task_environment.h"
#include "components/input/native_web_keyboard_event.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/input/web_input_event.h"

#if BUILDFLAG(ARKWEB_DELEGATE_COMPOSITING)

#define private public
#define protected public
#include "cef/libcef/browser/osr/browser_platform_delegate_osr.h"
#include "cef/libcef/browser/native/browser_platform_delegate_native.h"
#include "cef/ohos_cef_ext/libcef/browser/osr/browser_platform_delegate_osr_utils.h"

namespace {

// Mock for CefBrowserPlatformDelegateNative to allow constructing
// CefBrowserPlatformDelegateOsr without a full browser environment.
class CefBrowserPlatformDelegateNativeMock
    : public CefBrowserPlatformDelegateNative {
 public:
  CefBrowserPlatformDelegateNativeMock()
      : CefBrowserPlatformDelegateNative(CefWindowInfo(), SK_ColorTRANSPARENT) {}
  input::NativeWebKeyboardEvent TranslateWebKeyEvent(
      const CefKeyEvent& key_event) const override {
    return input::NativeWebKeyboardEvent(blink::WebInputEvent::Type::kKeyDown,
                                         0, base::TimeTicks());
  }
  blink::WebMouseEvent TranslateWebClickEvent(
      const CefMouseEvent& mouse_event,
      CefBrowserHost::MouseButtonType type,
      bool mouseUp,
      int clickCount) const override {
    return blink::WebMouseEvent();
  }
  blink::WebMouseEvent TranslateWebMoveEvent(
      const CefMouseEvent& mouse_event,
      bool mouseLeave) const override {
    return blink::WebMouseEvent();
  }
  blink::WebMouseWheelEvent TranslateWebWheelEvent(
      const CefMouseEvent& mouse_event,
      int deltaX,
      int deltaY) const override {
    return blink::WebMouseWheelEvent();
  }
#if BUILDFLAG(ARKWEB_TOUCHPAD_FLING)
  blink::WebGestureEvent TranslateTouchpadFlingEvent(
      const CefMouseEvent& mouse_event) const override {
    return blink::WebGestureEvent();
  }
#endif
};

class CefBrowserPlatformDelegateOsrUtilsTest : public testing::Test {
 protected:
  void SetUp() override {
    task_environment_ = std::make_unique<base::test::TaskEnvironment>(
        base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  }

  void TearDown() override { task_environment_.reset(); }

  std::unique_ptr<base::test::TaskEnvironment> task_environment_;
};

// ============================================================================
// DiscardCompositingResources Tests
// ============================================================================

// cefBrowserPlatformDelegateOsr is nullptr -> early return
TEST_F(CefBrowserPlatformDelegateOsrUtilsTest,
       DiscardCompositingResources_NullDelegate) {
  CefBrowserPlatformDelegateOsrUtils utils(nullptr);
  ASSERT_EQ(nullptr, utils.cefBrowserPlatformDelegateOsr.get());
  utils.DiscardCompositingResources();
  // Method returns early without accessing web_contents_
  ASSERT_EQ(nullptr, utils.cefBrowserPlatformDelegateOsr.get());
}

// cefBrowserPlatformDelegateOsr is valid but web_contents_ is nullptr -> early return
TEST_F(CefBrowserPlatformDelegateOsrUtilsTest,
       DiscardCompositingResources_NullWebContents) {
  auto osr_delegate = new CefBrowserPlatformDelegateOsr(
      std::make_unique<CefBrowserPlatformDelegateNativeMock>(), false, false);
  CefBrowserPlatformDelegateOsrUtils utils(osr_delegate);
  ASSERT_NE(nullptr, utils.cefBrowserPlatformDelegateOsr.get());
  ASSERT_EQ(nullptr, osr_delegate->web_contents_);
  utils.DiscardCompositingResources();
  // Method returns early because web_contents_ is null
  ASSERT_EQ(nullptr, osr_delegate->web_contents_);
}

}  // namespace

#endif  // BUILDFLAG(ARKWEB_DELEGATE_COMPOSITING)
