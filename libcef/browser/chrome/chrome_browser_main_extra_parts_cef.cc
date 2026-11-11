// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/libcef/browser/chrome/chrome_browser_main_extra_parts_cef.h"

#include "base/task/thread_pool.h"
#include "cef/libcef/browser/alloy/dialogs/alloy_constrained_window_views_client.h"
#include "cef/libcef/browser/chrome/chrome_context_menu_handler.h"
#include "cef/libcef/browser/chrome/chrome_startup_browser_creator.h"
#include "cef/libcef/browser/context.h"
#include "cef/libcef/browser/file_dialog_runner.h"
#include "cef/libcef/browser/permission_prompt.h"
#include "cef/libcef/common/app_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/views/chrome_constrained_window_views_client.h"
#include "components/constrained_window/constrained_window_views.h"

#if BUILDFLAG(IS_LINUX)
#include "base/linux_util.h"
#include "cef/libcef/browser/printing/print_dialog_linux.h"
#endif

#if BUILDFLAG(IS_WIN)
#include "chrome/browser/win/app_icon.h"
#endif

ChromeBrowserMainExtraPartsCef::ChromeBrowserMainExtraPartsCef() = default;

ChromeBrowserMainExtraPartsCef::~ChromeBrowserMainExtraPartsCef() {
#if BUILDFLAG(IS_LINUX)
  // Clear the global printing delegate pointer before |printing_delegate_| is
  // destroyed so it does not dangle. |print_dialog_factory_| unregisters itself
  // via the PrintDialogLinuxFactory destructor.
  if (printing_delegate_) {
    ui::PrintingContextLinuxDelegate::SetInstance(nullptr);
  }
#endif
}

void ChromeBrowserMainExtraPartsCef::PostProfileInit(Profile* profile,
                                                     bool is_initial_profile) {
  if (!is_initial_profile) {
    return;
  }

  CefRequestContextSettings settings;
  CefContext::Get()->PopulateGlobalRequestContextSettings(&settings);

  // Use the existing path for the initial profile.
  CefString(&settings.cache_path) = profile->GetPath().value();

  // Create the global RequestContext.
  global_request_context_ =
      CefRequestContextImpl::CreateGlobalRequestContext(settings);

#if BUILDFLAG(ARKWEB_COOKIE)
  CefCookieManagerExt::GetGlobalManager(nullptr);
#endif
#if BUILDFLAG(ARKWEB_INCOGNITO_MODE)
  // Create the Global off the record request context.
  CefRequestContextSettings off_the_record_settings;
  CefContext::Get()->PopulateGlobalOTRRequestContextSettings(
      &off_the_record_settings);
  global_otr_request_context_ =
      ArkWebRequestContextImplExt::CreateGlobalOTRRequestContext(
          off_the_record_settings);
  CefCookieManagerExt::GetGlobalIncognitoManager(nullptr);
#endif

#if BUILDFLAG(ARKWEB_NO_STATE_PREFETCH)
  if (base::CommandLine::ForCurrentProcess() &&
      !base::CommandLine::ForCurrentProcess()->HasSwitch(
        switches::kDisableAutoPreconnect)) {
    std::set<std::string> urls =
        predictor::PredictorDatabase::GetInstance()->GetRecentVisitedUrl();
    if (CefBrowserContext::GetAll().size() > 0) {
      auto browser_context = CefBrowserContext::GetAll()[0];
      ohos_predictors::LoadingPredictor* predictor =
          ohos_predictors::LoadingPredictorFactory::GetForBrowserContext(
              browser_context->AsBrowserContext());
      if (!predictor) {
        LOG(ERROR) << "loading predictor is null.";
        return;
      }
      for (auto& url : urls) {
        predictor->PrepareForPageLoad(GURL(url),
                                      ohos_predictors::HintOrigin::OMNIBOX, true);
      }

      std::vector<predictor::PreconnectUrlInfo> preconnect_url_infos =
          std::move(predictor::PredictorDatabase::preconnect_url_info_list);
      for (auto& preconnect_url_info : preconnect_url_infos) {
        predictor->PrepareForPageLoad(GURL(preconnect_url_info.url),
                                      ohos_predictors::HintOrigin::OMNIBOX,
                                      preconnect_url_info.is_preconnectable,
                                      preconnect_url_info.num_sockets);
      }
    }
  }
#endif  // ARKWEB_NO_STATE_PREFETCH
}

void ChromeBrowserMainExtraPartsCef::PostBrowserStart() {
  // Register the callback before ChromeBrowserMainParts::PostBrowserStart
  // allows ProcessSingleton to begin processing messages.
  startup_browser_creator::RegisterProcessCommandLineCallback();

#if BUILDFLAG(IS_LINUX)
  // This may be called indirectly via StartupBrowserCreator::LaunchBrowser.
  // Call it here before blocking is disallowed to avoid assertions.
  base::GetLinuxDistro();
#endif
}

void ChromeBrowserMainExtraPartsCef::PreMainMessageLoopRun() {
  // Register custom schemes with ChildProcessSecurityPolicy now that
  // FeatureList has been initialized.
  CefAppManager::Get()->RegisterCustomSchemesWithPolicy();

  background_task_runner_ = base::ThreadPool::CreateSingleThreadTaskRunner(
      {base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN, base::MayBlock()});
  user_visible_task_runner_ = base::ThreadPool::CreateSingleThreadTaskRunner(
      {base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN, base::MayBlock()});
  user_blocking_task_runner_ = base::ThreadPool::CreateSingleThreadTaskRunner(
      {base::TaskPriority::USER_BLOCKING,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN, base::MayBlock()});

  context_menu::RegisterCallbacks();
  file_dialog_runner::RegisterFactory();
  permission_prompt::RegisterCreateCallback();

#if BUILDFLAG(IS_WIN)
  const auto& settings = CefContext::Get()->settings();
  if (settings.chrome_app_icon_id > 0) {
    SetExeAppIconResourceId(settings.chrome_app_icon_id);
  }
#endif

#if BUILDFLAG(ARKWEB_ADBLOCK)
  if (g_browser_process) {
    g_browser_process->subresource_filter_ruleset_service();
  }
#endif
}

#if BUILDFLAG(ARKWEB_ENCRYPT)
void ChromeBrowserMainExtraPartsCef::PostCreateMainMessageLoop() {
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();

  // Set up crypt config. This needs to be done before anything starts the
  // network service, as the raw encryption key needs to be shared with the
  // network service for encrypted cookie storage.
  // Based on ChromeBrowserMainPartsLinux::PostCreateMainMessageLoop.
  std::unique_ptr<os_crypt::Config> config =
      std::make_unique<os_crypt::Config>();
  // Forward to os_crypt the flag to use a specific password store.
  config->store =
      command_line->GetSwitchValueASCII(password_manager::kPasswordStore);
  // Forward the product name (defaults to "Chromium").
  config->product_name = l10n_util::GetStringUTF8(IDS_PRODUCT_NAME);
  // OSCrypt may target keyring, which requires calls from the main thread.
  // config->main_thread_runner = content::GetUIThreadTaskRunner({});
  // OSCrypt can be disabled in a special settings file.
  config->should_use_preference =
      command_line->HasSwitch(password_manager::kEnableEncryptionSelection);
  base::PathService::Get(chrome::DIR_USER_DATA, &config->user_data_path);
  DCHECK(!config->user_data_path.empty());
  OSCrypt::SetConfig(std::move(config));
}
#endif

void ChromeBrowserMainExtraPartsCef::ToolkitInitialized() {
  // Override the default Chrome client.
  SetConstrainedWindowViewsClient(CreateAlloyConstrainedWindowViewsClient(
      CreateChromeConstrainedWindowViewsClient()));

#if BUILDFLAG(IS_LINUX)
  printing_delegate_ = std::make_unique<CefPrintingContextLinuxDelegate>();
  auto* default_delegate =
      ui::PrintingContextLinuxDelegate::SetInstance(printing_delegate_.get());
  printing_delegate_->SetDefaultDelegate(default_delegate);

  // Replace the default factory registered by
  // ChromeBrowserMainExtraPartsViewsLinux with a CefPrintDialogFactory, which
  // routes browsers that have a CefPrintHandler through the delegate above and
  // otherwise falls back to its PrintDialogLinuxFactory base (preserving the
  // normal portal/LinuxUi dialog selection). SetPrintDialogFactory() only
  // accepts a new factory when none is currently set, so clear the existing one
  // first; the PrintDialogLinuxFactory base constructor then registers this
  // instance. Owned by |print_dialog_factory_| so it unregisters itself at
  // shutdown.
  printing::PrintingContextLinux::SetPrintDialogFactory(nullptr);
  print_dialog_factory_ = std::make_unique<CefPrintDialogFactory>();
#endif  // BUILDFLAG(IS_LINUX)
}
