// Copyright (c) 2024 Huawei Device Co., Ltd.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CEF_OHOS_CEF_EXT_INCLUDE_ARKWEB_LANGUAGE_DETECTION_DETAILS_H_
#define CEF_OHOS_CEF_EXT_INCLUDE_ARKWEB_LANGUAGE_DETECTION_DETAILS_H_

#include <string>

struct ArkWebLanguageDetectionDetails {
  std::string url;
  std::string content_language;
  std::string html_root_language;
  std::string model_detected_language;
  std::string adopted_language;

  bool IsEmpty() const { return adopted_language.empty(); }

  void Reset() {
    url.clear();
    content_language.clear();
    html_root_language.clear();
    model_detected_language.clear();
    adopted_language.clear();
  }
};

#endif  // CEF_OHOS_CEF_EXT_INCLUDE_ARKWEB_LANGUAGE_DETECTION_DETAILS_H_
