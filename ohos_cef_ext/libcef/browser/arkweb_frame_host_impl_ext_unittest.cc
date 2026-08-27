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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "arkweb/build/features/features.h"

#if BUILDFLAG(ARKWEB_INPUT_EVENTS)

#include "arkweb/ohos_nweb/include/nweb_errors.h"
#include "cef/libcef/browser/browser_info.h"
#include "cef/ohos_cef_ext/libcef/browser/arkweb_frame_host_impl_ext.h"
#include "content/public/test/browser_task_environment.h"

using OHOS::NWeb::WebCommandErrorCode;

namespace {

scoped_refptr<CefBrowserInfo> CreateTestBrowserInfo() {
  cef::BrowserConfig config = cef::BrowserConfig{
      /*is_windowless=*/true,
      /*print_preview_enabled=*/false,
      /*move_pip_enabled=*/false,
      /*allow_pip_without_user_activation=*/false,
  };
  return base::MakeRefCounted<CefBrowserInfo>(
      /*browser_id=*/1,
      /*is_popup=*/false, config, CefRefPtr<CefDictionaryValue>());
}

}  // namespace

class ArkwebFrameHostExtImplTest : public ::testing::Test {
 protected:
  void SetUp() override {
    browser_info_ = CreateTestBrowserInfo();
    frame_ = new ArkwebFrameHostExtImpl(browser_info_, std::nullopt);
  }

  void TearDown() override {
    frame_ = nullptr;
  }

  content::BrowserTaskEnvironment task_environment_;
  scoped_refptr<CefBrowserInfo> browser_info_;
  CefRefPtr<ArkwebFrameHostExtImpl> frame_;
};

// ===== ExecuteInputCommand / ExecuteSelectCommand guard checks =====
// For a newly constructed frame without a browser, render_frame_host_ is null,
// so these methods should return PAGE_NOT_READY early.

/**
 * @tc.name: ExecuteInputCommand_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteInputCommand returns PAGE_NOT_READY when render_frame_host_
 *           is null (frame not yet attached to a browser).
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteInputCommand_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteInputCommand(
      "//input[@id='name']", "hello", "input-text");
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

/**
 * @tc.name: ExecuteSelectCommand_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteSelectCommand returns PAGE_NOT_READY when render_frame_host_
 *           is null (frame not yet attached to a browser).
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteSelectCommand_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteSelectCommand(
      "//select[@id='color']", {0, 1}, {"red", "blue"});
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

// ===== New Execute*Command guard checks =====
// All new methods should return PAGE_NOT_READY when render_frame_host_ is null,
// same pattern as ExecuteInputCommand/ExecuteSelectCommand.

/**
 * @tc.name: ExecuteFocusCommand_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteFocusCommand returns PAGE_NOT_READY when render_frame_host_
 *           is null.
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteFocusCommand_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteFocusCommand("//input[@id='name']", true);
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

/**
 * @tc.name: ExecuteInputModifyCommand_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteInputModifyCommand returns PAGE_NOT_READY when
 *           render_frame_host_ is null.
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteInputModifyCommand_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteInputModifyCommand("hello");
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

/**
 * @tc.name: ExecuteInputInsertCommand_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteInputInsertCommand returns PAGE_NOT_READY when
 *           render_frame_host_ is null.
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteInputInsertCommand_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteInputInsertCommand("hello", 0);
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

/**
 * @tc.name: ExecuteClickCommand_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteClickCommand returns PAGE_NOT_READY when render_frame_host_
 *           is null.
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteClickCommand_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteClickCommand("//button[@id='submit']");
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

/**
 * @tc.name: ExecuteScrollCommand_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteScrollCommand returns PAGE_NOT_READY when render_frame_host_
 *           is null.
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteScrollCommand_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteScrollCommand("//div[@id='target']", "top", 0);
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

/**
 * @tc.name: ExecuteInputSelectCommand_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteInputSelectCommand returns PAGE_NOT_READY when
 *           render_frame_host_ is null.
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteInputSelectCommand_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteInputSelectCommand(0, 5);
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

/**
 * @tc.name: ExecuteInputSetCursorCommand_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteInputSetCursorCommand returns PAGE_NOT_READY when
 *           render_frame_host_ is null.
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteInputSetCursorCommand_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteInputSetCursorCommand(0);
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

/**
 * @tc.name: ExecuteAIPageInputCommandByXPath_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteAIPageInputCommandByXPath returns PAGE_NOT_READY when
 *           render_frame_host_ is null.
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteAIPageInputCommandByXPath_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteAIPageInputCommandByXPath(
      "//input[@id='name']", "hello", "default");
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

/**
 * @tc.name: ExecuteSelectCommandById_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteSelectCommandById returns PAGE_NOT_READY when
 *           render_frame_host_ is null.
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteSelectCommandById_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteSelectCommandById("color", {0}, {});
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

/**
 * @tc.name: ExecuteAIPageInputCommandById_NoRenderFrame_ReturnsPageNotReady_0100
 * @tc.desc: ExecuteAIPageInputCommandById returns PAGE_NOT_READY when
 *           render_frame_host_ is null.
 * @tc.type: FUNC
 */
TEST_F(ArkwebFrameHostExtImplTest,
       ExecuteAIPageInputCommandById_NoRenderFrame_ReturnsPageNotReady_0100) {
  auto result = frame_->ExecuteAIPageInputCommandById("name", "hello", "default");
  EXPECT_EQ(result,
            static_cast<int32_t>(WebCommandErrorCode::PAGE_NOT_READY));
}

#endif  // BUILDFLAG(ARKWEB_INPUT_EVENTS)
