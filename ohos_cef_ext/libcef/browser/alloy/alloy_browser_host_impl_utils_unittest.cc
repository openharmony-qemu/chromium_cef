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
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(ARKWEB_DELEGATE_COMPOSITING)

#define private public
#define protected public
#include "cef/ohos_cef_ext/libcef/browser/alloy/alloy_browser_host_impl_utils.h"

namespace {

class AlloyBrowserHostImplUtilsTest : public testing::Test {
 protected:
  void SetUp() override {
    task_environment_ = std::make_unique<base::test::TaskEnvironment>(
        base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  }

  void TearDown() override { task_environment_.reset(); }

  std::unique_ptr<base::test::TaskEnvironment> task_environment_;
};

// ============================================================================
// DiscardCompositingResources
// Gate1: alloyBrowserHostImpl is nullptr -> early return
// ============================================================================

// Gate1: alloyBrowserHostImpl is nullptr -> early return
TEST_F(AlloyBrowserHostImplUtilsTest,
       DiscardCompositingResources_NullImpl) {
  AlloyBrowserHostImplUtils utils(nullptr);
  ASSERT_EQ(nullptr, utils.alloyBrowserHostImpl.get());
  utils.DiscardCompositingResources();
  ASSERT_EQ(nullptr, utils.alloyBrowserHostImpl.get());
}

}  // namespace

#endif  // BUILDFLAG(ARKWEB_DELEGATE_COMPOSITING)
