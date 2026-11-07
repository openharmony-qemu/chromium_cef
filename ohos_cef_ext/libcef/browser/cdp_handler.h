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

#ifndef CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_CDP_HANDLER_H_
#define CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_CDP_HANDLER_H_

class CdpHandler final {
 public:
  static CdpHandler& GetInstance();

  void BrowserClose();

  CdpHandler(const CdpHandler&) = delete;
  CdpHandler& operator=(const CdpHandler&) = delete;

 private:
  CdpHandler() = default;
};

#endif // CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_CDP_HANDLER_H_
