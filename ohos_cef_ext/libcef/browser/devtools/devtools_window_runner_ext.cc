/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "build/buildflag.h"
#include "arkweb/build/features/features.h"

#if BUILDFLAG(ARKWEB_DEVTOOLS)
#include "cef/libcef/browser/devtools/devtools_window_runner.h"

#include "cef/ohos_cef_ext/libcef/browser/devtools/arkweb/devtools_frontend.h"

raw_ptr<CefDevToolsFrontend> CefDevToolsWindowRunner::static_devtools_frontend_ = nullptr;
void CefDevToolsWindowRunner::OnFrontEndDestroyed() {
  LOG(INFO) << "OnFrontEndDestroyed";
  devtools_frontend_ = nullptr;
}

void CefDevToolsWindowRunner::OnStaticFrontEndDestroyed() {
  LOG(INFO) << "OnStaticFrontEndDestroyed";
  static_devtools_frontend_ = nullptr;
}

void CefDevToolsWindowRunner::StaticShowDevToolsWith(
    const CefString& source_id,
    const CefString& target_id,
    const CefString& type,
    CefRefPtr<ArkWebBrowserHostExt> frontend_browser,
    CefRefPtr<CefDevToolsMessageHandlerDelegate> devtools_message_handler,
    const CefPoint& inspect_element_at,
    const CefOpenDevToolsExtOpt& ext_opt) {
  LOG(INFO) << "CefDevToolsWindowRunner::StaticShowDevToolsWith";
  CEF_REQUIRE_UIT();
  auto* arkweb_host_ext = static_cast<ArkWebBrowserHostExtImpl*>(frontend_browser.get());
  auto alloy_frontend_browser = arkweb_host_ext->AsAlloyBrowserHostImpl();
  if (static_devtools_frontend_ &&
      static_devtools_frontend_->GetFrontendBrowser() == alloy_frontend_browser.get()) {
    LOG(INFO) << "ShowDevToolsWith, reuse devtools_frontend";
    // 作用：复用静态DevTools前端时重置session级设置
    // 合理性：OH浏览器hide/show DevTools复用CefDevToolsFrontend对象，需模拟Chrome SESSION存储销毁的session边界行为
    // 必要性：不重置则JS禁用等session级设置跨DevTools会话残留，用户关闭再打开后JS仍被禁用
    if (!inspect_element_at.IsEmpty()) {
      static_devtools_frontend_->InspectElementAt(inspect_element_at.x,
                                           inspect_element_at.y);
    }
    static_devtools_frontend_->Focus();
    return;
  }

  if (!alloy_frontend_browser) {
    NOTIMPLEMENTED();
  } else {
    static_devtools_frontend_ = CefDevToolsFrontend::StaticShowWith(
        source_id,
        target_id,
        type,
        alloy_frontend_browser.get(), std::move(devtools_message_handler),
        nullptr, inspect_element_at,
        base::BindOnce(&CefDevToolsWindowRunner::OnStaticFrontEndDestroyed),
        ext_opt);
  }

}

void CefDevToolsWindowRunner::ShowDevToolsWith(
    CefRefPtr<ArkWebBrowserHostExt> frontend_browser,
    CefBrowserHostBase* inspected_browser,
    CefRefPtr<CefDevToolsMessageHandlerDelegate> devtools_message_handler,
    const CefPoint& inspect_element_at) {
  CEF_REQUIRE_UIT();
  auto* arkweb_host_ext = static_cast<ArkWebBrowserHostExtImpl*>(frontend_browser.get());
  auto alloy_frontend_browser = arkweb_host_ext->AsAlloyBrowserHostImpl();
  if (devtools_frontend_ &&
      devtools_frontend_->GetFrontendBrowser() == alloy_frontend_browser.get()) {
    LOG(INFO) << "ShowDevToolsWith, reuse devtools_frontend";
    // 作用：复用已有DevTools前端时重置session级设置
    // 合理性：OH浏览器hide/show DevTools时复用CefDevToolsFrontend对象，需模拟session边界重置JS禁用等状态
    // 必要性：不重置则用户感知的"关闭再打开"周期内JS禁用状态持续残留
    if (!inspect_element_at.IsEmpty()) {
      devtools_frontend_->InspectElementAt(inspect_element_at.x,
                                           inspect_element_at.y);
    }
    devtools_frontend_->Focus();
    return;
  }

  if (!alloy_frontend_browser) {
    NOTIMPLEMENTED();
  } else {
    auto* web_contents = inspected_browser->GetWebContents();
    devtools_frontend_ = CefDevToolsFrontend::ShowWith(
        alloy_frontend_browser.get(), std::move(devtools_message_handler),
        web_contents, inspect_element_at,
        base::BindOnce(&CefDevToolsWindowRunner::OnFrontEndDestroyed,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

void CefDevToolsWindowRunner::ShowDevToolsWithByPb(
    CefRefPtr<ArkWebBrowserHostExt> frontend_browser,
    CefBrowserHostBase* inspected_browser,
    CefRefPtr<CefDevToolsMessageHandlerDelegate> devtools_message_handler,
    const CefPoint& inspect_element_at,
    const CefOpenDevToolsExtOpt& ext_opt) {
  CEF_REQUIRE_UIT();
  auto* arkweb_host_ext = static_cast<ArkWebBrowserHostExtImpl*>(frontend_browser.get());
  auto alloy_frontend_browser = arkweb_host_ext->AsAlloyBrowserHostImpl();
  if (devtools_frontend_ &&
      devtools_frontend_->GetFrontendBrowser() == alloy_frontend_browser.get()) {
    LOG(INFO) << "ShowDevToolsWithByPb, reuse devtools_frontend";
    // 作用：复用已有DevTools前端时重置session级设置
    // 合理性：与ShowDevToolsWith相同的复用路径，通过Pb（PanelBar）方式打开时也需要模拟session边界
    // 必要性：不重置则JS禁用等session级状态跨DevTools会话残留
    if (!inspect_element_at.IsEmpty()) {
      devtools_frontend_->InspectElementAt(inspect_element_at.x,
                                           inspect_element_at.y);
    }
    devtools_frontend_->Focus();
    return;
  }

  if (!alloy_frontend_browser) {
    NOTIMPLEMENTED();
  } else {
    auto* web_contents = inspected_browser->GetWebContents();
    devtools_frontend_ = CefDevToolsFrontend::ShowWithByPb(
        alloy_frontend_browser.get(), std::move(devtools_message_handler),
        web_contents, inspect_element_at,
        base::BindOnce(&CefDevToolsWindowRunner::OnFrontEndDestroyed,
                       weak_ptr_factory_.GetWeakPtr()),
        ext_opt);
  }
}
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)
