/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#ifndef CEF_LIBCEF_BROWSER_DEVTOOLS_ARKWEB_DEVTOOLS_FRONTEND_H_
#define CEF_LIBCEF_BROWSER_DEVTOOLS_ARKWEB_DEVTOOLS_FRONTEND_H_

#include <memory>

#include "arkweb/build/features/features.h"

#if BUILDFLAG(ARKWEB_DEVTOOLS)
// 作用：引入unordered_set用于维护存活DevTools前端实例的静态注册表
// 合理性：Chrome用GetDevToolsWindowInstances()全局vector管理DevToolsWindow实例，CEF用unordered_set实现等价查找
// 必要性：弹出窗口自动打开DevTools需要遍历所有存活实例查找opener的DevTools，无此容器则无法建立opener→DevTools的映射
#include <unordered_set>
#endif

#include "libcef/browser/alloy/alloy_browser_host_impl.h"
#include "libcef/browser/devtools/arkweb/devtools_file_manager.h"

#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_frontend_host.h"
#include "content/public/browser/web_contents_observer.h"

#if BUILDFLAG(ARKWEB_DEVTOOLS)
#include "include/cef_devtools_message_handler_delegate.h"
#include "libcef/browser/devtools/arkweb/devtools_message_handler.h"
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

namespace base {
class Value;
}

namespace content {
class NavigationHandle;
class RenderViewHost;
class WebContents;
}  // namespace content

class PrefService;

enum class ProtocolMessageType {
  METHOD,
  RESULT,
  EVENT,
};

class CefDevToolsFrontend : public content::WebContentsObserver,
#if BUILDFLAG(ARKWEB_DEVTOOLS)
                            public CefDevToolsFileManager::Delegate,
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)
                            public content::DevToolsAgentHostClient {
 public:
  CefDevToolsFrontend(const CefDevToolsFrontend&) = delete;
  CefDevToolsFrontend& operator=(const CefDevToolsFrontend&) = delete;

  static CefDevToolsFrontend* Show(
      AlloyBrowserHostImpl* inspected_browser,
      const CefWindowInfo& windowInfo,
      CefRefPtr<CefClient> client,
      const CefBrowserSettings& settings,
      const CefPoint& inspect_element_at,
      base::OnceClosure frontend_destroyed_callback);

#if BUILDFLAG(ARKWEB_DEVTOOLS)
  static CefDevToolsFrontend* StaticShowWith(
    const CefString& source_id,
    const CefString& target_id,
    const CefString& type,
    AlloyBrowserHostImpl* frontend_browser,
    CefRefPtr<CefDevToolsMessageHandlerDelegate> devtools_message_handler,
    content::WebContents* inspected_contents,
    const CefPoint& inspect_element_at,
    base::OnceClosure frontend_destroyed_callback,
    const CefOpenDevToolsExtOpt& ext_opt);
  static CefDevToolsFrontend* ShowWith(
      AlloyBrowserHostImpl* frontend_browser,
      CefRefPtr<CefDevToolsMessageHandlerDelegate> devtools_message_handler,
      content::WebContents* inspected_contents,
      const CefPoint& inspect_element_at,
      base::OnceClosure frontend_destroyed_callback);

  static CefDevToolsFrontend* ShowWithByPb(
      AlloyBrowserHostImpl* frontend_browser,
      CefRefPtr<CefDevToolsMessageHandlerDelegate> devtools_message_handler,
      content::WebContents* inspected_contents,
      const CefPoint& inspect_element_at,
      base::OnceClosure frontend_destroyed_callback,
      const CefOpenDevToolsExtOpt& ext_opt);
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

  void Activate();
  void Focus();
  void InspectElementAt(int x, int y);
  void Close();

  void CallClientFunction(
      const std::string& object_name,
      const std::string& method_name,
      const base::Value arg1 = {},
      const base::Value arg2 = {},
      const base::Value arg3 = {},
      base::OnceCallback<void(base::Value)> cb = base::NullCallback());

#if BUILDFLAG(ARKWEB_DEVTOOLS)
  AlloyBrowserHostImpl* GetFrontendBrowser();

  // 作用：提供按被调试WebContents查找CefDevToolsFrontend实例的静态方法
  // 合理性：Chrome通过全局vector+GetInstanceForInspectedWebContents实现，CEF用unordered_set+线性扫描实现等价功能
  // 必要性：MaybeAutoOpenDevToolsForPopup需判断弹出窗口的opener是否已有DevTools实例，无此查找则无法确定自动打开条件
  static CefDevToolsFrontend* FindByInspectedContents(
      content::WebContents* contents);

  // 作用：弹出窗口创建时的自动打开DevTools入口
  // 合理性：遍历opener链查找启用了auto-attach的DevTools实例，与Chrome的DevToolsWindow::SetOpenNewWindowForPopups逻辑对齐
  // 必要性：不实现则"Auto-open DevTools for popups"设置无法工作
  static void MaybeAutoOpenDevToolsForPopup(
      content::WebContents* popup_contents);

  // 作用：沿opener链查找启用了自动打开标志的opener WebContents
  // 合理性：遍历opener链查找open_new_window_for_popups_为true的DevTools实例对应的被调试页面
  // 必要性：MaybeAutoOpenDevToolsForPopup的opener链遍历逻辑提取为独立方法以降低圈复杂度和嵌套深度
  static content::WebContents* FindAutoOpenOpenerInChain(
      content::WebContents* popup_contents);

  // 作用：从PrefService读取auto-attach-to-created-pages偏好
  // 合理性：opener链未命中时回退到持久化偏好读取，覆盖三个字典
  // 必要性：dock模式切换重建CefDevToolsFrontend后FindByInspectedContents可能失败，偏好回退是必要兜底
  static bool ReadAutoAttachFromPrefs(content::WebContents* opener);

  // 作用：构造自动打开DevTools的RequestOpenDevToolsParams
  // 合理性：opener链命中和偏好回退两条路径都需构造相同参数，提取为公共方法避免重复
  // 必要性：两处重复代码提取后降低nbnc行数
  static RequestOpenDevToolsParams BuildAutoOpenParams(
      content::WebContents* opener, content::WebContents* popup);

  // 作用：触发弹出窗口自动打开DevTools的核心执行方法；合理性：优先走per-NWeb V2路径保留docking上下文，静态回调路径丢失docking信息；必要性：直接使用静态路径会导致docked模式打开为独立窗口
  static void TriggerAutoOpenForPopup(
      content::WebContents* popup_contents,
      const RequestOpenDevToolsParams& params);

  // 作用：获取"自动为弹出窗口打开DevTools"标志；合理性：MaybeAutoOpenDevToolsForPopup需要读取此标志判断是否触发自动打开；必要性：不暴露此getter则opener链遍历时无法判断自动打开条件
  bool open_new_window_for_popups() const {
    return open_new_window_for_popups_;
  }
#endif  // BUILDFLAG(ARKWEB_DEVTOOLS)

 private:
  CefDevToolsFrontend(AlloyBrowserHostImpl* frontend_browser,
                      content::WebContents* inspected_contents,
                      const CefPoint& inspect_element_at,
                      base::OnceClosure destroyed_callback);
#if BUILDFLAG(ARKWEB_DEVTOOLS)
  CefDevToolsFrontend(AlloyBrowserHostImpl* frontend_browser,
                      std::unique_ptr<CefDevToolsMessageHandler>
                          devtools_message_handler,
                      content::WebContents* inspected_contents,
                      const CefPoint& inspect_element_at,
                      base::OnceClosure destroyed_callback);
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)
  ~CefDevToolsFrontend() override;

  // content::DevToolsAgentHostClient implementation.
  void AgentHostClosed(content::DevToolsAgentHost* agent_host) override;
  void DispatchProtocolMessage(content::DevToolsAgentHost* agent_host,
                               base::span<const uint8_t> message) override;
  void HandleMessageFromDevToolsFrontend(base::Value::Dict message);

#if BUILDFLAG(ARKWEB_DEVTOOLS)
  // CefDevToolsFileManager::Delegate implementation.
  void FileSystemAdded(
      const std::string& error,
      const CefDevToolsFileManager::FileSystem* file_system) override;
  void FileSystemRemoved(const std::string& file_system_path) override;
  void FilePathsChanged(
      const std::vector<std::string>& changed_paths,
      const std::vector<std::string>& added_paths,
      const std::vector<std::string>& removed_paths) override;
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

 private:
  // WebContentsObserver overrides
  void ReadyToCommitNavigation(
      content::NavigationHandle* navigation_handle) override;
  void PrimaryMainDocumentElementAvailable() override;
  void WebContentsDestroyed() override;

  void SendMessageAck(int request_id, base::Value::Dict arg);

  bool ProtocolLoggingEnabled() const;
  void LogProtocolMessage(ProtocolMessageType type, const std::string_view& message);

  PrefService* GetPrefs() const;

#if BUILDFLAG(ARKWEB_DEVTOOLS)
  void SendMessageAck(int request_id, base::Value arg);
  void RequestFileSystems();
  bool ShouldUseTabTarget();

  // 作用：拦截CDP setScriptExecutionDisabled命令并同步WebContentsImpl状态
  // 合理性：从HandleMessageFromDevToolsFrontend中提取以降低嵌套深度
  // 必要性：前端可能通过JSON消息路径直接发送CDP命令
  void InterceptScriptExecutionDisabled(const std::string& protocol_message);

  // 作用：处理loadCompleted前端消息
  // 合理性：从HandleMessageFromDevToolsFrontend中提取以降低嵌套深度
  // 必要性：保持原有setInitialTargetId+extensions+setUseSoftMenu逻辑不变
  void HandleLoadCompleted();
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)
#if BUILDFLAG(ARKWEB_ARKWEB_EXTENSIONS)
  base::Value::Dict BuildExtensionInfo(const extensions::Extension*);
  void AddDevToolsExtensionsToClient();
  void SetSourceTargetId(const CefString& source_id, const CefString& target_id);
#endif // BUILDFLAG(ARKWEB_ARKWEB_EXTENSIONS)

  CefRefPtr<AlloyBrowserHostImpl> frontend_browser_;
#if BUILDFLAG(ARKWEB_CRASHPAD)
  base::WeakPtr<content::WebContents> inspected_contents_;
#else
  raw_ptr<content::WebContents> inspected_contents_;
#endif
  scoped_refptr<content::DevToolsAgentHost> agent_host_;
  CefPoint inspect_element_at_;
  base::OnceClosure frontend_destroyed_callback_;
  std::unique_ptr<content::DevToolsFrontendHost> frontend_host_;

  class NetworkResourceLoader;
  std::set<std::unique_ptr<NetworkResourceLoader>, base::UniquePtrComparator>
      loaders_;

  using ExtensionsAPIs = std::map<std::string, std::string>;
  ExtensionsAPIs extensions_api_;
  CefDevToolsFileManager file_manager_;

  const base::FilePath protocol_log_file_;

#if BUILDFLAG(ARKWEB_DEVTOOLS)
  std::unique_ptr<CefDevToolsMessageHandler> devtools_message_handler_;
  bool isTabTarget_ = false;
  bool show_toolbox_ = false;
  // 作用：存储"自动为弹出窗口打开DevTools"的偏好标志
  // 合理性：对应前端"auto-attach-to-created-pages"设置，在构造时从PrefService读取并在setOpenNewWindowForPopups消息时更新
  // 必要性：dock模式切换会销毁旧CefDevToolsFrontend创建新的，此标志必须在构造时从持久化存储恢复，否则切换后弹出窗口自动打开失效
  bool open_new_window_for_popups_ = false;
  CefString source_id_;
  CefString target_id_;

  // 作用：返回存活DevTools前端实例的静态注册表引用
  // 合理性：使用NoDestructor避免进程退出时析构顺序问题，与Chrome的GetDevToolsWindowInstances()设计对齐
  // 必要性：FindByInspectedContents需要遍历所有存活实例，构造/析构时需注册/注销，无此注册表则opener链查找无法实现
  static std::unordered_set<CefDevToolsFrontend*>& GetLiveInstances();
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

  base::WeakPtrFactory<CefDevToolsFrontend> weak_factory_;
};

#endif  // CEF_LIBCEF_BROWSER_DEVTOOLS_ARKWEB_DEVTOOLS_FRONTEND_H_
