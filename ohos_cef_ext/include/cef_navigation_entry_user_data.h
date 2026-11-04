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

#ifndef CEF_OHOS_CEF_EXT_INCLUDE_CEF_NAVIGATION_ENTRY_USER_DATA_H_
#define CEF_OHOS_CEF_EXT_INCLUDE_CEF_NAVIGATION_ENTRY_USER_DATA_H_
#pragma once

#include <map>
#include <memory>
#include <string>

#include "base/supports_user_data.h"

// Session-scoped user data attached to a content::NavigationEntry through the
// base::SupportsUserData mechanism. The address of |kKey| is used as the
// user data key, which isolates this data from other SupportsUserData
// consumers. Values live in memory only and are never persisted; they are
// deep-copied when the owning navigation entry is cloned (back/forward
// history) via Clone().
class CefNavigationEntryUserData : public base::SupportsUserData::Data {
 public:
  // The static address of this object is the SupportsUserData key.
  static const char kKey;

  // Sets |value| for |key|. Silently ignored when |key| or |value| is empty.
  void SetUserData(const std::string& key, const std::string& value);

  // Returns the value for |key|, or an empty string when |key| does not
  // exist or is empty.
  std::string GetUserData(const std::string& key) const;

  // Removes |key|. Silently ignored when |key| does not exist or is empty.
  void RemoveUserData(const std::string& key);

  // base::SupportsUserData::Data:
  // Returns a new instance holding a copy of the internal map.
  std::unique_ptr<Data> Clone() override;

 private:
  std::map<std::string, std::string> user_data_map_;
};

#endif  // CEF_OHOS_CEF_EXT_INCLUDE_CEF_NAVIGATION_ENTRY_USER_DATA_H_
