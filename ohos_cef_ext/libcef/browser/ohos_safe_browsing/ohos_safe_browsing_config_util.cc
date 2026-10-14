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
 
#include "ohos_safe_browsing_config_util.h"

#include "arkweb/build/features/features.h"
#if BUILDFLAG(IS_ARKWEB_EXT)
#include "arkweb/ohos_nweb_ex/build/features/features.h"
#endif
#include "base/command_line.h"
#include "base/memory/raw_ptr.h"
#include "content/public/common/content_switches.h"
 
#if BUILDFLAG(ARKWEB_EXT_SAFEBROWSING)
#include "arkweb/ohos_nweb_ex/overrides/cef/libcef/browser/alloy/alloy_ark_web_safe_browsing_config.h"
#endif
 
namespace ohos_safe_browsing {
SafeBrowsingConfigUtil& SafeBrowsingConfigUtil::GetInstance() {
  static SafeBrowsingConfigUtil instance;
  return instance;
}
 
bool SafeBrowsingConfigUtil::OptimizeEnable() {
#if BUILDFLAG(ARKWEB_EXT_SAFEBROWSING)
  auto safeBrowsingConfig =
      raw_ptr<nweb_ex::AlloyArkWebSafeBrowsingConfig>(nweb_ex::AlloyArkWebSafeBrowsingConfig::GetInstance());
  if (safeBrowsingConfig->OptimizeEnable()) {
    return true;
  }
#endif
  return base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kEnableSafeBrowserOptimize);
}
 
int SafeBrowsingConfigUtil::GetMaliciousUrlCheckWaitTime() {
#if BUILDFLAG(ARKWEB_EXT_SAFEBROWSING)
  auto safeBrowsingConfig =
      raw_ptr<nweb_ex::AlloyArkWebSafeBrowsingConfig>(nweb_ex::AlloyArkWebSafeBrowsingConfig::GetInstance());
  return safeBrowsingConfig->GetMaliciousUrlCheckWaitTime();
#else
  return 0;
#endif
}

bool SafeBrowsingConfigUtil::SbOverlayErrorPage() {
#if BUILDFLAG(ARKWEB_EXT_SAFEBROWSING)
  auto safeBrowsingConfig =
      raw_ptr<nweb_ex::AlloyArkWebSafeBrowsingConfig>(nweb_ex::AlloyArkWebSafeBrowsingConfig::GetInstance());
  return safeBrowsingConfig->SbOverlayErrorPage();
#else
  return false;
#endif
}
 
}  // namespace ohos_safe_browsing
