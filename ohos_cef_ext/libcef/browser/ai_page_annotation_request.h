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

#ifndef CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_AI_PAGE_ANNOTATION_REQUEST_H_
#define CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_AI_PAGE_ANNOTATION_REQUEST_H_
#pragma once

#include <string>

#include "base/memory/ref_counted.h"
#include "cef/include/cef_base.h"
#include "cef/ohos_cef_ext/include/arkweb_frame_ext.h"

class ArkwebFrameHostExtImpl;
class CefBrowserInfo;

namespace arkweb::ai_page {

void RunAddPageAnnotation(scoped_refptr<CefBrowserInfo> browser_info,
                          CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                          std::string params,
                          CefRefPtr<CefAIPageCommandCallback> callback);

void RunRemovePageAnnotation(scoped_refptr<CefBrowserInfo> browser_info,
                             CefRefPtr<ArkwebFrameHostExtImpl> root_frame,
                             CefRefPtr<CefAIPageCommandCallback> callback);

} // namespace arkweb::ai_page

#endif // CEF_OHOS_CEF_EXT_LIBCEF_BROWSER_AI_PAGE_ANNOTATION_REQUEST_H_
