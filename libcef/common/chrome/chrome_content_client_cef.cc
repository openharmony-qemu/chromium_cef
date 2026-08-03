// Copyright 2015 The Chromium Embedded Framework Authors.
// Portions copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/common/chrome/chrome_content_client_cef.h"

#include "cef/libcef/common/app_manager.h"
#include "chrome/common/media/cdm_registration.h"

#if !BUILDFLAG(ARKWEB_ENABLE_CDM) && !defined(COMPONENT_BUILD)
#if BUILDFLAG(ENABLE_CDM_HOST_VERIFICATION)
#include "cef/libcef/common/cdm_host_file_path.h"
#endif
#endif

ChromeContentClientCef::ChromeContentClientCef() = default;
ChromeContentClientCef::~ChromeContentClientCef() = default;

void ChromeContentClientCef::AddContentDecryptionModules(
    std::vector<content::CdmInfo>* cdms,
    std::vector<media::CdmHostFilePath>* cdm_host_file_paths) {
  if (cdms) {
    RegisterCdmInfo(cdms);
  }

#if !BUILDFLAG(ARKWEB_ENABLE_CDM) && !defined(COMPONENT_BUILD)
#if BUILDFLAG(ENABLE_CDM_HOST_VERIFICATION)
  if (cdm_host_file_paths) {
    cef::AddCdmHostFilePaths(cdm_host_file_paths);
  }
#endif
#endif
}

void ChromeContentClientCef::AddAdditionalSchemes(Schemes* schemes) {
  ChromeContentClient::AddAdditionalSchemes(schemes);
  CefAppManager::Get()->AddAdditionalSchemes(schemes);
}

#if BUILDFLAG(IS_ARKWEB)
bool ChromeContentClientCef::ShouldAllowDefaultSiteInstanceGroup() {
  // ArkWeb needs to keep the M132 subframe sharing behavior.
  // Disabling kDefaultSiteInstanceGroups allows cross-origin subframes to share
  // the parent SiteInstance. This matches other ad WebView's approach.
  // See crbug.com/419595581
  return false;
}
#endif
