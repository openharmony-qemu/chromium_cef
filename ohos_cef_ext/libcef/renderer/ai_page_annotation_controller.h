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

#ifndef CEF_OHOS_CEF_EXT_LIBCEF_RENDERER_AI_PAGE_ANNOTATION_CONTROLLER_H_
#define CEF_OHOS_CEF_EXT_LIBCEF_RENDERER_AI_PAGE_ANNOTATION_CONTROLLER_H_
#pragma once

#include <string>

#include "build/build_config.h"

namespace blink {
class WebLocalFrame;
} // namespace blink

#if BUILDFLAG(IS_OHOS)
namespace arkweb::ai_page {

std::string BuildAIPageElementRectsResult(blink::WebLocalFrame *frame,
                                          const std::string &params);
std::string RenderPageAnnotationOverlay(blink::WebLocalFrame *frame,
                                        const std::string &params);
std::string RemovePageAnnotationOverlay(blink::WebLocalFrame *frame);

} // namespace arkweb::ai_page
#endif // BUILDFLAG(IS_OHOS)

#endif // CEF_OHOS_CEF_EXT_LIBCEF_RENDERER_AI_PAGE_ANNOTATION_CONTROLLER_H_
