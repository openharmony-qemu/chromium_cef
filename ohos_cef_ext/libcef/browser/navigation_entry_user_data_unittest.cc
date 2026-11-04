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

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>

#include "base/supports_user_data.h"
#include "cef/ohos_cef_ext/include/cef_navigation_entry_user_data.h"

namespace {

// AC-5.1: Set -> Get roundtrip returns the exact value.
TEST(CefNavigationEntryUserData, SetGetRemoveRoundtrip) {
  CefNavigationEntryUserData data;
  data.SetUserData("key1", "value1");
  EXPECT_EQ("value1", data.GetUserData("key1"));
  data.RemoveUserData("key1");
  EXPECT_EQ(std::string(), data.GetUserData("key1"));
}

// AC-1.5/AC-5.1: same key overwrites the previous value.
TEST(CefNavigationEntryUserData, OverwriteSameKey) {
  CefNavigationEntryUserData data;
  data.SetUserData("key1", "value1");
  data.SetUserData("key1", "value2");
  EXPECT_EQ("value2", data.GetUserData("key1"));
}

// EB-3: remove a missing key is a silent no-op.
TEST(CefNavigationEntryUserData, RemoveMissingKeyNoOp) {
  CefNavigationEntryUserData data;
  data.SetUserData("key1", "value1");
  data.RemoveUserData("not_exist");
  EXPECT_EQ("value1", data.GetUserData("key1"));
}

// EB-3: get a missing key returns an empty string.
TEST(CefNavigationEntryUserData, GetMissingKeyEmpty) {
  CefNavigationEntryUserData data;
  EXPECT_EQ(std::string(), data.GetUserData("not_exist"));
}

// EB-4: set with an empty key is silently ignored.
TEST(CefNavigationEntryUserData, SetEmptyKeyIgnored) {
  CefNavigationEntryUserData data;
  data.SetUserData("", "value1");
  EXPECT_EQ(std::string(), data.GetUserData(""));
}

// EB-4: set with an empty value is silently ignored.
TEST(CefNavigationEntryUserData, SetEmptyValueIgnored) {
  CefNavigationEntryUserData data;
  data.SetUserData("key1", "");
  EXPECT_EQ(std::string(), data.GetUserData("key1"));
}

// AC-5.2: clone preserves all keys and values.
TEST(CefNavigationEntryUserData, ClonePreservesAllKeys) {
  CefNavigationEntryUserData data;
  data.SetUserData("key1", "value1");
  data.SetUserData("key2", "value2");
  auto clone = data.Clone();
  auto* cloned = static_cast<CefNavigationEntryUserData*>(clone.get());
  ASSERT_NE(nullptr, cloned);
  EXPECT_EQ("value1", cloned->GetUserData("key1"));
  EXPECT_EQ("value2", cloned->GetUserData("key2"));
}

// AC-5.2: clone is a deep copy; mutations do not affect each other.
TEST(CefNavigationEntryUserData, CloneDeepCopyIndependent) {
  CefNavigationEntryUserData data;
  data.SetUserData("key1", "value1");
  auto clone = data.Clone();
  auto* cloned = static_cast<CefNavigationEntryUserData*>(clone.get());
  ASSERT_NE(nullptr, cloned);

  cloned->SetUserData("key1", "changed");
  cloned->SetUserData("key2", "new_value");
  EXPECT_EQ("value1", data.GetUserData("key1"));
  EXPECT_EQ(std::string(), data.GetUserData("key2"));

  data.RemoveUserData("key1");
  EXPECT_EQ("changed", cloned->GetUserData("key1"));
}

// AC-1.1/AC-1.2: supports the SupportsUserData storage pattern used by
// content::NavigationEntry, including CloneDataFrom propagation (the path
// taken when a navigation entry is cloned for back/forward history).
TEST(CefNavigationEntryUserData, SupportsUserDataStorageAndCloneDataFrom) {
  class TestHost : public base::SupportsUserData {};

  TestHost host;
  EXPECT_EQ(nullptr, host.GetUserData(&CefNavigationEntryUserData::kKey));

  host.SetUserData(&CefNavigationEntryUserData::kKey,
                   std::make_unique<CefNavigationEntryUserData>());
  auto* stored = static_cast<CefNavigationEntryUserData*>(
      host.GetUserData(&CefNavigationEntryUserData::kKey));
  ASSERT_NE(nullptr, stored);
  stored->SetUserData("key1", "value1");

  TestHost cloned_host;
  cloned_host.CloneDataFrom(host);
  auto* cloned = static_cast<CefNavigationEntryUserData*>(
      cloned_host.GetUserData(&CefNavigationEntryUserData::kKey));
  ASSERT_NE(nullptr, cloned);
  EXPECT_EQ("value1", cloned->GetUserData("key1"));

  cloned->SetUserData("key1", "changed");
  EXPECT_EQ("value1", stored->GetUserData("key1"));

  host.RemoveUserData(&CefNavigationEntryUserData::kKey);
  EXPECT_EQ(nullptr, host.GetUserData(&CefNavigationEntryUserData::kKey));
}

}  // namespace
