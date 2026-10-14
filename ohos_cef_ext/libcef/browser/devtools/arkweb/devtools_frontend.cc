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

#include "libcef/browser/devtools/arkweb/devtools_frontend.h"

#include <cstddef>
#include <iomanip>
#include <utility>

#include "libcef/browser/browser_context.h"
#include "libcef/browser/devtools/devtools_manager_delegate.h"
#include "libcef/browser/net/devtools_scheme_handler.h"
#include "libcef/browser/thread_util.h"
#include "libcef/common/cef_switches.h"
#include "libcef/common/task_runner_manager.h"

#include "base/base64.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/guid.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_task_traits.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/file_url_loader.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/shared_cors_origin_access_list.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_client.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/url_utils.h"
#include "ipc/ipc_channel.h"
#include "net/base/completion_once_callback.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/cpp/simple_url_loader_stream_consumer.h"
#include "services/network/public/cpp/wrapper_shared_url_loader_factory.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "storage/browser/file_system/native_file_util.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#elif BUILDFLAG(IS_POSIX)
#include <time.h>
#endif

#if BUILDFLAG(ARKWEB_ARKWEB_EXTENSIONS)
#include "cef/ohos_cef_ext/libcef/browser/chrome/extensions/arkweb_chrome_extension_util_ext.h"
#include "chrome/browser/extensions/extension_management.h"
#include "chrome/browser/extensions/extension_service.h"
#include "extensions/common/manifest_url_handlers.h"
#include "extensions/common/manifest_handlers/devtools_page_handler.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_util.h"
#include "extensions/common/mojom/api_permission_id.mojom-shared.h"
#include "extensions/common/permissions/permissions_data.h"
#endif // BUILDFLAG(ARKWEB_ARKWEB_EXTENSIONS)
#if BUILDFLAG(ARKWEB_DEVTOOLS)
#include "chrome/browser/profiles/profile.h"
#include "ohos_nweb/src/capi/nweb_devtools_message_handler.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "content/browser/web_contents/web_contents_impl.h"
// 作用：引入NoDestructor用于GetLiveInstances()静态注册表，避免进程退出时析构顺序问题
#include "base/no_destructor.h"
// 作用：引入NWebImpl等OH扩展头文件以支持弹出窗口自动打开DevTools和per-NWeb回调路径
#include "ohos_nweb/src/nweb_impl.h"
#include "cef/libcef/browser/alloy/alloy_browser_host_impl.h"
#include "cef/ohos_cef_ext/libcef/browser/alloy/alloy_browser_host_impl_ext.h"
#include "cef/ohos_cef_ext/libcef/browser/arkweb_browser_host_ext.h"
#include "libcef/browser/devtools/arkweb/request_open_devtools_handler.h"
// 作用：引入DevToolsAgentHost用于从target_id解析WebContents
#include "content/public/browser/devtools_agent_host.h"
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

namespace {

// This constant should be in sync with the constant in
// chrome/browser/devtools/devtools_ui_bindings.cc.
constexpr size_t kMaxMessageChunkSize = IPC::mojom::kChannelMaximumMessageSize / 4;

constexpr int kMaxLogLineLength = 1024;

#if BUILDFLAG(ARKWEB_DEVTOOLS)
static const char kTitleFormat[] = "DevTools - %s";
static std::string GetFrontendURL(bool can_dock, bool is_tab_target,
                                  bool show_toolbox, const std::string& panel = "") {
  LOG(DEBUG) << "GetFrontendURL can_dock: " << can_dock
             << ", is_tab_target: " << is_tab_target
             << ", show_toolbox: " << show_toolbox;
  return base::StringPrintf(
      "%s://%s/devtools_app.html?can_dock=%s%s%s%s",
      content::kChromeDevToolsScheme,
      scheme::kChromeDevToolsHost,
      can_dock ? "true" : "false",
      is_tab_target ? "&targetType=tab" : "",
      show_toolbox ? "&showToolbox=true" : "",
      !panel.empty() ? "&panel=" + panel : "");
}
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

static std::string GetFrontendURL() {
  return base::StringPrintf("%s://%s/devtools_app.html",
                            content::kChromeDevToolsScheme,
                            scheme::kChromeDevToolsHost);
}

base::Value::Dict BuildObjectForResponse(const net::HttpResponseHeaders* rh,
                                         bool success,
                                         int net_error) {
  base::Value::Dict response;
  int responseCode = 200;
  if (rh) {
    responseCode = rh->response_code();
  } else if (!success) {
    // In case of no headers, assume file:// URL and failed to load
    responseCode = 404;
  }
  response.Set("statusCode", responseCode);
  response.Set("netError", net_error);
  response.Set("netErrorName", net::ErrorToString(net_error));

  base::Value::Dict headers;
  size_t iterator = 0;
  std::string name;
  std::string value;
  // TODO(caseq): this probably needs to handle duplicate header names
  // correctly by folding them.
  while (rh && rh->EnumerateHeaderLines(&iterator, &name, &value)) {
    headers.Set(name, value);
  }

  response.Set("headers", std::move(headers));
  return response;
}

void WriteTimestamp(std::stringstream& stream) {
#if BUILDFLAG(IS_WIN)
  SYSTEMTIME local_time;
  GetLocalTime(&local_time);
  stream << std::setfill('0') << std::setw(2) << local_time.wMonth
         << std::setw(2) << local_time.wDay << '/' << std::setw(2)
         << local_time.wHour << std::setw(2) << local_time.wMinute
         << std::setw(2) << local_time.wSecond << '.' << std::setw(3)
         << local_time.wMilliseconds;
#elif BUILDFLAG(IS_POSIX)
  timeval tv;
  gettimeofday(&tv, nullptr);
  time_t t = tv.tv_sec;
  struct tm local_time;
  localtime_r(&t, &local_time);
  struct tm* tm_time = &local_time;
  stream << std::setfill('0') << std::setw(2) << 1 + tm_time->tm_mon
         << std::setw(2) << tm_time->tm_mday << '/' << std::setw(2)
         << tm_time->tm_hour << std::setw(2) << tm_time->tm_min << std::setw(2)
         << tm_time->tm_sec << '.' << std::setw(6) << tv.tv_usec;
#else
#error Unsupported platform
#endif
}

void LogProtocolMessage(const base::FilePath& log_file,
                        ProtocolMessageType type,
                        std::string to_log) {
  // Track if logging has failed, in which case we don't keep trying.
  static bool log_error = false;
  if (log_error) {
    return;
  }

  if (storage::NativeFileUtil::EnsureFileExists(log_file, nullptr) !=
      base::File::FILE_OK) {
    LOG(ERROR) << "Failed to create file " << log_file.value();
    log_error = true;
    return;
  }

  std::string type_label;
  switch (type) {
    case ProtocolMessageType::METHOD:
      type_label = "METHOD";
      break;
    case ProtocolMessageType::RESULT:
      type_label = "RESULT";
      break;
    case ProtocolMessageType::EVENT:
      type_label = "EVENT";
      break;
  }

  std::stringstream stream;
  WriteTimestamp(stream);
  stream << ": " << type_label << ": " << to_log << "\n";
  const std::string& str = stream.str();
  if (!base::AppendToFile(log_file, std::string_view(str))) {
    LOG(ERROR) << "Failed to write file " << log_file.value();
    log_error = true;
  }
}

#if BUILDFLAG(ARKWEB_DEVTOOLS)
base::Value::Dict CreateFileSystemValue(
    CefDevToolsFileManager::FileSystem file_system) {
  base::Value::Dict file_system_value;
  file_system_value.Set("type", file_system.type);
  file_system_value.Set("fileSystemName", file_system.file_system_name);
  file_system_value.Set("rootURL", file_system.root_url);
  file_system_value.Set("fileSystemPath", file_system.file_system_path);
  return file_system_value;
}
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

}  // namespace

#if BUILDFLAG(ARKWEB_DEVTOOLS)
// static
// 作用：返回所有活跃CefDevToolsFrontend实例的静态集合
// 合理性：使用base::NoDestructor避免退出时析构警告，类比Chrome的GetDevToolsWindowInstances()
// 必要性：弹出窗口自动打开DevTools需遍历活跃实例查找opener对应的DevTools，没有全局集合无法实现反查
std::unordered_set<CefDevToolsFrontend*>& CefDevToolsFrontend::GetLiveInstances() {
  static base::NoDestructor<std::unordered_set<CefDevToolsFrontend*>> instances;
  return *instances;
}
#endif  // BUILDFLAG(ARKWEB_DEVTOOLS)

class CefDevToolsFrontend::NetworkResourceLoader
    : public network::SimpleURLLoaderStreamConsumer {
 public:
  NetworkResourceLoader(int stream_id,
                        CefDevToolsFrontend* bindings,
                        std::unique_ptr<network::SimpleURLLoader> loader,
                        network::mojom::URLLoaderFactory* url_loader_factory,
                        int request_id)
      : stream_id_(stream_id),
#if BUILDFLAG(ARKWEB_CRASHPAD)
        bindings_(bindings->weak_factory_.GetWeakPtr()),
#else
        bindings_(bindings),
#endif
        loader_(std::move(loader)),
        request_id_(request_id) {
    loader_->SetOnResponseStartedCallback(base::BindOnce(
        &NetworkResourceLoader::OnResponseStarted, base::Unretained(this)));
    loader_->DownloadAsStream(url_loader_factory, this);
  }

  NetworkResourceLoader(const NetworkResourceLoader&) = delete;
  NetworkResourceLoader& operator=(const NetworkResourceLoader&) = delete;

 private:
  void OnResponseStarted(const GURL& final_url,
                         const network::mojom::URLResponseHead& response_head) {
    response_headers_ = response_head.headers;
  }

  void OnDataReceived(std::string_view chunk,
                      base::OnceClosure resume) override {
#if BUILDFLAG(ARKWEB_CRASHPAD)
    if (!bindings_) {
      std::move(resume).Run();
      return;
    }
#endif
    base::Value chunkValue;

    bool encoded = !base::IsStringUTF8(chunk);
    if (encoded) {
      chunkValue = base::Value(base::Base64Encode(chunk));
    } else {
      chunkValue = base::Value(chunk);
    }
    base::Value id(stream_id_);
    base::Value encodedValue(encoded);
#if BUILDFLAG(ARKWEB_CRASHPAD)
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif
    bindings_->CallClientFunction("DevToolsAPI", "streamWrite", std::move(id),
                                  std::move(chunkValue),
                                  std::move(encodedValue));
    std::move(resume).Run();
  }

  void OnComplete(bool success) override {
#if BUILDFLAG(ARKWEB_CRASHPAD)
    if (!bindings_) {
      return;
    }
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
    auto response = BuildObjectForResponse(response_headers_.get(), success,
                                           loader_->NetError());
    auto it = bindings_->loaders_.find(this);
    bindings_->SendMessageAck(request_id_, std::move(response));
    bindings_->loaders_.erase(it);
#else
    auto response = BuildObjectForResponse(response_headers_.get(), success,
                                           loader_->NetError());
    bindings_->SendMessageAck(request_id_, std::move(response));

    bindings_->loaders_.erase(bindings_->loaders_.find(this));
#endif
  }

  void OnRetry(base::OnceClosure start_retry) override { DCHECK(false); }

  const int stream_id_;
#if BUILDFLAG(ARKWEB_CRASHPAD)
  base::WeakPtr<CefDevToolsFrontend> bindings_;
#else
  raw_ptr<CefDevToolsFrontend> const bindings_;
#endif
  std::unique_ptr<network::SimpleURLLoader> loader_;
  int request_id_;
  scoped_refptr<net::HttpResponseHeaders> response_headers_;
};

// static
CefDevToolsFrontend* CefDevToolsFrontend::Show(
    AlloyBrowserHostImpl* inspected_browser,
    const CefWindowInfo& windowInfo,
    CefRefPtr<CefClient> client,
    const CefBrowserSettings& settings,
    const CefPoint& inspect_element_at,
    base::OnceClosure frontend_destroyed_callback) {
  CefBrowserSettings new_settings = settings;
  if (!windowInfo.windowless_rendering_enabled &&
      CefColorGetA(new_settings.background_color) != SK_AlphaOPAQUE) {
    // Use white as the default background color for windowed DevTools instead
    // of the CefSettings.background_color value.
    new_settings.background_color = SK_ColorWHITE;
  }
#if BUILDFLAG(ARKWEB_DEVTOOLS)
  if (!inspected_browser) {
    LOG(ERROR) << " func:" << __func__ << " inspected_browser is null.";
    return nullptr;
  }
#endif // ARKWEB_DEVTOOLS
  CefBrowserCreateParams create_params;
  if (inspected_browser->is_views_hosted()) {
    create_params.popup_with_views_hosted_opener = true;
  } else {
    create_params.window_info.reset(new CefWindowInfo(windowInfo));
  }
  create_params.client = client;
  create_params.settings = new_settings;
  create_params.devtools_opener = inspected_browser;
  create_params.request_context = inspected_browser->GetRequestContext();
  create_params.extra_info = inspected_browser->browser_info()->extra_info();

  CefRefPtr<AlloyBrowserHostImpl> frontend_browser =
      AlloyBrowserHostImpl::Create(create_params);

  content::WebContents* inspected_contents = inspected_browser->web_contents();

  // CefDevToolsFrontend will delete itself when the frontend WebContents is
  // destroyed.
  CefDevToolsFrontend* devtools_frontend = new CefDevToolsFrontend(
      static_cast<AlloyBrowserHostImpl*>(frontend_browser.get()),
      inspected_contents, inspect_element_at,
      std::move(frontend_destroyed_callback));

  // Need to load the URL after creating the DevTools objects.
  frontend_browser->GetMainFrame()->LoadURL(GetFrontendURL());

  return devtools_frontend;
}

#if BUILDFLAG(ARKWEB_DEVTOOLS)
// static
CefDevToolsFrontend* CefDevToolsFrontend::StaticShowWith(
      const CefString& source_id,
      const CefString& target_id,
      const CefString& type,
      AlloyBrowserHostImpl* frontend_browser,
      CefRefPtr<CefDevToolsMessageHandlerDelegate> devtools_message_handler,
      content::WebContents* inspected_contents,
      const CefPoint& inspect_element_at,
      base::OnceClosure frontend_destroyed_callback,
      const CefOpenDevToolsExtOpt& ext_opt) {
  LOG(INFO) << "CefDevToolsFrontend::StaticShowWith";

  // 作用：当未传入inspected_contents但提供了target_id时，通过DevToolsAgentHost反查WebContents
  // 合理性：弹出窗口自动打开DevTools只传target_id，需反查才能设置isTabTarget_并生成正确的tab类型URL
  // 必要性：不反查则inspected_contents_为null导致isTabTarget_=false，URL缺少&targetType=tab参数，Elements面板为空
  if (!inspected_contents && !target_id.empty()) {
    // 作用：根据target_id获取DevToolsAgentHost
    // 合理性：target_id是DevToolsAgentHost的唯一标识，GetForId是标准查找接口
    // 必要性：弹窗自动打开流程只携带target_id，必须通过此接口还原WebContents引用
    scoped_refptr<content::DevToolsAgentHost> host =
        content::DevToolsAgentHost::GetForId(target_id.ToString());
    if (host) {
      // 作用：从agent host获取被调试的WebContents
      // 合理性：DevToolsAgentHost持有被调试目标的WebContents引用
      // 必要性：后续isTabTarget_判断和URL生成都依赖inspected_contents
      inspected_contents = host->GetWebContents();
    }
  }

  auto handler = std::make_unique<CefDevToolsMessageHandler>(
      std::move(devtools_message_handler),
      Profile::FromBrowserContext(
          frontend_browser->web_contents()->GetBrowserContext()),
      ext_opt);

  // CefDevToolsFrontend will delete itself when the frontend WebContents is
  // destroyed.
  CefDevToolsFrontend* devtools_frontend = new CefDevToolsFrontend(
      frontend_browser,
      std::move(handler),
      inspected_contents, inspect_element_at,
      std::move(frontend_destroyed_callback));

  if (devtools_frontend) {
    devtools_frontend->show_toolbox_ = ext_opt.showToolbox;
    devtools_frontend->SetSourceTargetId(source_id, target_id);
#if BUILDFLAG(ARKWEB_CRASHPAD)
    DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif

    // Need to load the URL after creating the DevTools objects.
    GURL devtoolsUrl("");
    std::string panel = "";
    if (ext_opt.event.has_value()) {
        panel = ext_opt.event->panel;
    }
    // 作用：对自动打开的DevTools强制启用canDock选项
    // 合理性：弹出窗口自动打开的DevTools应与手动打开一样支持停靠模式，type=="auto"标识自动打开场景
    // 必要性：不强制启用则自动打开的DevTools无法切换停靠模式，用户体验不一致
    bool effective_can_dock = ext_opt.canDock;
    if (type.ToString() == "auto") {
      // 作用：自动打开场景下将canDock强制设为true
      // 合理性：ext_opt.canDock可能为false（默认），但自动打开的DevTools需要停靠能力
      // 必要性：不设为true则URL中can_dock参数为false，前端禁用停靠选项
      effective_can_dock = true;
    }
    if (type.ToString() == "Service workers" ||
        type.ToString() == "Shared workers") {
      devtoolsUrl = DevToolsWindow::GetDevToolsURL(nullptr,
                                                   DevToolsWindow::kFrontendWorker,
                                                   "",
                                                   effective_can_dock,
                                                   panel,
                                                   false,
                                                   false);
    } else {
      devtoolsUrl = GURL(GetFrontendURL(
          effective_can_dock, devtools_frontend->isTabTarget_, devtools_frontend->show_toolbox_, panel));

    }
    frontend_browser->GetMainFrame()->LoadURL(devtoolsUrl.spec());
  }
  return devtools_frontend;
}

// static
CefDevToolsFrontend* CefDevToolsFrontend::ShowWith(
      AlloyBrowserHostImpl* frontend_browser,
      CefRefPtr<CefDevToolsMessageHandlerDelegate> devtools_message_handler,
      content::WebContents* inspected_contents,
      const CefPoint& inspect_element_at,
      base::OnceClosure frontend_destroyed_callback) {
  LOG(INFO) << "CefDevToolsFrontend::ShowWith({"
            << inspect_element_at.x << "*" << inspect_element_at.y << "})";
  CefOpenDevToolsExtOpt ext_opt;
  auto handler = std::make_unique<CefDevToolsMessageHandler>(
      std::move(devtools_message_handler),
      Profile::FromBrowserContext(
          frontend_browser->web_contents()->GetBrowserContext()),
      ext_opt);
  // CefDevToolsFrontend will delete itself when the frontend WebContents is
  // destroyed.
  CefDevToolsFrontend* devtools_frontend = new CefDevToolsFrontend(
      frontend_browser, std::move(handler),
      inspected_contents, inspect_element_at,
      std::move(frontend_destroyed_callback));
#if BUILDFLAG(ARKWEB_CRASHPAD)
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif
  // Need to load the URL after creating the DevTools objects.
  frontend_browser->GetMainFrame()->LoadURL(GetFrontendURL(false, devtools_frontend->isTabTarget_, false));

  return devtools_frontend;
}

// static
CefDevToolsFrontend* CefDevToolsFrontend::ShowWithByPb(
      AlloyBrowserHostImpl* frontend_browser,
      CefRefPtr<CefDevToolsMessageHandlerDelegate> devtools_message_handler,
      content::WebContents* inspected_contents,
      const CefPoint& inspect_element_at,
      base::OnceClosure frontend_destroyed_callback,
      const CefOpenDevToolsExtOpt& ext_opt) {
  LOG(INFO) << "CefDevToolsFrontend::ShowWithByPb({"
            << inspect_element_at.x << "*" << inspect_element_at.y << "})"
            << ", canDock: " << ext_opt.canDock << ", showToolbox: " << ext_opt.showToolbox;
  auto handler = std::make_unique<CefDevToolsMessageHandler>(
      std::move(devtools_message_handler),
      Profile::FromBrowserContext(
          frontend_browser->web_contents()->GetBrowserContext()),
      ext_opt);
  // CefDevToolsFrontend will delete itself when the frontend WebContents is
  // destroyed.
  CefDevToolsFrontend* devtools_frontend = new CefDevToolsFrontend(
      frontend_browser, std::move(handler),
      inspected_contents, inspect_element_at,
      std::move(frontend_destroyed_callback));
  devtools_frontend->show_toolbox_ = ext_opt.showToolbox;

  std::string panel = "";
  if (ext_opt.event.has_value()) {
    panel = ext_opt.event->panel;
  }
  // Need to load the URL after creating the DevTools objects.
  frontend_browser->GetMainFrame()->LoadURL(
    GetFrontendURL(ext_opt.canDock, devtools_frontend->isTabTarget_, devtools_frontend->show_toolbox_, panel));

  return devtools_frontend;
}
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

void CefDevToolsFrontend::Activate() {
#if BUILDFLAG(ARKWEB_CRASHPAD)
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif
  frontend_browser_->ActivateContents(web_contents());
}

void CefDevToolsFrontend::Focus() {
#if BUILDFLAG(ARKWEB_CRASHPAD)
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif
  frontend_browser_->SetFocus(true);
}

void CefDevToolsFrontend::InspectElementAt(int x, int y) {
  if (inspect_element_at_.x != x || inspect_element_at_.y != y) {
    inspect_element_at_.Set(x, y);
  }
#if BUILDFLAG(ARKWEB_CRASHPAD)
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (agent_host_ && inspected_contents_) {
    agent_host_->InspectElement(inspected_contents_->GetFocusedFrame(), x, y);
  }
#else
  if (agent_host_) {
    agent_host_->InspectElement(inspected_contents_->GetFocusedFrame(), x, y);
  }
#endif

}

void CefDevToolsFrontend::Close() {
#if BUILDFLAG(ARKWEB_CRASHPAD)
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif
  // Check if frontend_browser_ is valid to avoid calling CloseBrowser on an uninitialized or destroyed object
  if (frontend_browser_) {
    CEF_POST_TASK(CEF_UIT, base::BindOnce(&AlloyBrowserHostImpl::CloseBrowser,
                                          frontend_browser_.get(), true));
  }
}

CefDevToolsFrontend::CefDevToolsFrontend(
    AlloyBrowserHostImpl* frontend_browser,
    content::WebContents* inspected_contents,
    const CefPoint& inspect_element_at,
    base::OnceClosure frontend_destroyed_callback)
    : content::WebContentsObserver(frontend_browser->web_contents()),
      frontend_browser_(frontend_browser),
#if BUILDFLAG(ARKWEB_CRASHPAD)
  inspected_contents_(inspected_contents ? inspected_contents->GetWeakPtr() : nullptr),
#else
  inspected_contents_(inspected_contents),
#endif

      inspect_element_at_(inspect_element_at),
      frontend_destroyed_callback_(std::move(frontend_destroyed_callback)),
      file_manager_(frontend_browser, GetPrefs()),
      protocol_log_file_(
          base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
              switches::kDevToolsProtocolLogFile)),
      weak_factory_(this) {
  DCHECK(!frontend_destroyed_callback_.is_null());
}

#if BUILDFLAG(ARKWEB_DEVTOOLS)
CefDevToolsFrontend::CefDevToolsFrontend(
    AlloyBrowserHostImpl* frontend_browser,
    std::unique_ptr<CefDevToolsMessageHandler> devtools_message_handler,
    content::WebContents* inspected_contents,
    const CefPoint& inspect_element_at,
    base::OnceClosure frontend_destroyed_callback)
    : content::WebContentsObserver(frontend_browser->web_contents()),
      frontend_browser_(frontend_browser),
#if BUILDFLAG(ARKWEB_CRASHPAD)
  inspected_contents_(inspected_contents ? inspected_contents->GetWeakPtr() : nullptr),
#else
  inspected_contents_(inspected_contents),
#endif
      inspect_element_at_(inspect_element_at),
      frontend_destroyed_callback_(std::move(frontend_destroyed_callback)),
      file_manager_(frontend_browser, this, GetPrefs()),
      protocol_log_file_(
          base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
              switches::kDevToolsProtocolLogFile)),
      devtools_message_handler_(std::move(devtools_message_handler)),
      weak_factory_(this) {
  DCHECK(!frontend_destroyed_callback_.is_null());

  // 作用：将本实例注册到静态注册表
  // 合理性：类比Chrome的GetDevToolsWindowInstances()，用全局集合管理活跃的DevTools前端实例
  // 必要性：弹出窗口自动打开DevTools时需通过opener链反查DevTools实例，不注册则FindByInspectedContents找不到对应实例导致自动打开失效
  GetLiveInstances().insert(this);

  // 作用：构造时从PrefService读取已持久化的"auto-attach-to-created-pages"偏好以初始化open_new_window_for_popups_
  // 合理性：该偏好是SYNCED全局偏好，存在PrefService中，构造时读取确保dock模式切换重建CefDevToolsFrontend后标志位正确
  // 必要性：不读取则新建实例标志位默认false，前端JS加载完成前若弹窗创建则自动打开失效
  {
    auto* prefs = GetPrefs();
    if (prefs) {
      // 作用：从主偏好字典读取auto-attach-to-created-pages；合理性：kDevToolsPreferences是DevTools偏好的主存储键；必要性：这是首选读取路径
      const auto& dict = prefs->GetDict(prefs::kDevToolsPreferences);
      auto* val = dict.Find("auto-attach-to-created-pages");
      if (val && val->is_bool()) {
        open_new_window_for_popups_ = val->GetBool();
      }
      // 作用：主偏好未命中时从synced偏好字典回退读取；合理性：DevTools偏好可能存在sync_enabled字典中（同步启用的偏好）；必要性：偏好存储位置不固定，需多字典回退确保读到
      if (!open_new_window_for_popups_) {
        const auto& sync_enabled = prefs->GetDict(prefs::kDevToolsSyncedPreferencesSyncEnabled);
        val = sync_enabled.Find("auto-attach-to-created-pages");
        if (val && val->is_bool()) {
          open_new_window_for_popups_ = val->GetBool();
        }
      }
      // 作用：从前两个字典都未命中时从sync_disabled字典回退读取；合理性：sync_disabled字典存储同步禁用的偏好，某些场景偏好落在此处；必要性：覆盖所有可能的偏好存储位置，确保标志位正确恢复
      if (!open_new_window_for_popups_) {
        const auto& sync_disabled = prefs->GetDict(prefs::kDevToolsSyncedPreferencesSyncDisabled);
        val = sync_disabled.Find("auto-attach-to-created-pages");
        if (val && val->is_bool()) {
          open_new_window_for_popups_ = val->GetBool();
        }
      }
    }
  }
  file_manager_.SetDevToolsMessageHandler(devtools_message_handler_.get());
  devtools_message_handler_->SetDevToolsFrontend(this);
  isTabTarget_ = ShouldUseTabTarget();
}
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

CefDevToolsFrontend::~CefDevToolsFrontend() {}

void CefDevToolsFrontend::ReadyToCommitNavigation(
    content::NavigationHandle* navigation_handle) {
#if BUILDFLAG(ARKWEB_CRASHPAD)
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif
  content::RenderFrameHost* frame = navigation_handle->GetRenderFrameHost();
  if (navigation_handle->IsInMainFrame()) {
#if BUILDFLAG(ARKWEB_DEVTOOLS)
    if (agent_host_.get()) {
      agent_host_->DetachClient(this);
      agent_host_->AttachClient(this);
    }
    if (frontend_host_) {
      return;
    }
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)
#if BUILDFLAG(ARKWEB_CRASHPAD)
    frontend_host_ = content::DevToolsFrontendHost::Create(
            frame, base::BindRepeating(
                       [](base::WeakPtr<CefDevToolsFrontend> self, base::Value::Dict message) {
                         if (self) {
                           self->HandleMessageFromDevToolsFrontend(std::move(message));
                         }
                       },
                       weak_factory_.GetWeakPtr()));
#else
    frontend_host_ = content::DevToolsFrontendHost::Create(
        frame, base::BindRepeating(
                   &CefDevToolsFrontend::HandleMessageFromDevToolsFrontend,
                   base::Unretained(this)));
#endif
    return;
  }

  std::string origin =
      navigation_handle->GetURL().DeprecatedGetOriginAsURL().spec();
  auto it = extensions_api_.find(origin);
  if (it == extensions_api_.end()) {
    return;
  }
  std::string script = base::StringPrintf("%s(\"%s\")", it->second.c_str(),
                                          base::Uuid::GenerateRandomV4().AsLowercaseString().c_str());
  content::DevToolsFrontendHost::SetupExtensionsAPI(frame, script);
}

void CefDevToolsFrontend::PrimaryMainDocumentElementAvailable() {
  // Don't call AttachClient multiple times for the same DevToolsAgentHost.
  // Otherwise it will call AgentHostClosed which closes the DevTools window.
  // This may happen in cases where the DevTools content fails to load.

#if BUILDFLAG(ARKWEB_DEVTOOLS)
  isTabTarget_ = ShouldUseTabTarget();
  scoped_refptr<content::DevToolsAgentHost> agent_host;
  if (!inspected_contents_) {
    content::DevToolsAgentHost::List targets = content::DevToolsAgentHost::GetOrCreateAll();
    for (const scoped_refptr<content::DevToolsAgentHost>& host : targets) {
      if (CefString(host->GetId()) == target_id_) {
        agent_host = host;
        LOG(INFO) << " func:" << __func__;
        break;
      }
    }
  }
#if BUILDFLAG(ARKWEB_CRASHPAD)
  else if (isTabTarget_) {
    agent_host = content::DevToolsAgentHost::GetOrCreateForTab(inspected_contents_.get());
  } else {
    agent_host = content::DevToolsAgentHost::GetOrCreateFor(inspected_contents_.get());
  }
#else
  else if (isTabTarget_) {
    agent_host = content::DevToolsAgentHost::GetOrCreateForTab(inspected_contents_);
  } else {
    agent_host = content::DevToolsAgentHost::GetOrCreateFor(inspected_contents_);
  }
#endif // ARKWEB_CRASHPAD
#else
#if BUILDFLAG(ARKWEB_CRASHPAD)
  agent_host = content::DevToolsAgentHost::GetOrCreateFor(inspected_contents_.get());
#else
  agent_host = content::DevToolsAgentHost::GetOrCreateFor(inspected_contents_);
#endif // ARKWEB_CRASHPAD
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

  if (agent_host != agent_host_) {
    if (agent_host_) {
      agent_host_->DetachClient(this);
    }
    agent_host_ = agent_host;
    agent_host_->AttachClient(this);
    if (!inspect_element_at_.IsEmpty()) {
#if BUILDFLAG(ARKWEB_DEVTOOLS)
#if BUILDFLAG(ARKWEB_CRASHPAD)
      if (!inspected_contents_) {
        return;
      }
#endif
      content::RenderFrameHost* rfh = inspected_contents_->GetFocusedFrame();
      if (!rfh) {
        rfh = inspected_contents_->GetPrimaryMainFrame();
      }
      if (!rfh) {
        return;
      }
      agent_host_->InspectElement(rfh,
                                  inspect_element_at_.x, inspect_element_at_.y);
#else
      agent_host_->InspectElement(inspected_contents_->GetFocusedFrame(),
                                  inspect_element_at_.x, inspect_element_at_.y);
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)
    }
  }
}

void CefDevToolsFrontend::WebContentsDestroyed() {
#if BUILDFLAG(ARKWEB_DEVTOOLS)
  // 作用：从静态注册表移除本实例；合理性：实例销毁前必须移除，避免悬空指针；必要性：不移除则FindByInspectedContents可能返回已销毁的实例导致UAF
  GetLiveInstances().erase(this);
#endif  // BUILDFLAG(ARKWEB_DEVTOOLS)
  if (agent_host_) {
    agent_host_->DetachClient(this);
    agent_host_ = nullptr;
  }
#if BUILDFLAG(ARKWEB_CRASHPAD)
  // Call the callback if it's not null
  // Note: The caller should use weak pointers when binding the callback
  // to avoid UAF issues
  if (frontend_destroyed_callback_) {
    std::move(frontend_destroyed_callback_).Run();
  }
#else
  std::move(frontend_destroyed_callback_).Run();
#endif
  delete this;
}

void CefDevToolsFrontend::HandleMessageFromDevToolsFrontend(
    base::Value::Dict message) {
#if BUILDFLAG(ARKWEB_CRASHPAD)
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif
  const std::string* method = message.FindString("method");
  if (!method) {
    return;
  }

  int request_id = message.FindInt("id").value_or(0);
  base::Value::List* params_value = message.FindList("params");

  // Since we've received message by value, we can take the list.
  base::Value::List params;
  if (params_value) {
    params = std::move(*params_value);
  }

#if BUILDFLAG(ARKWEB_DEVTOOLS)
  if (devtools_message_handler_) {
    auto ret = devtools_message_handler_->HandleMessage(
        request_id, *method, params);
    if (ret.handled) {
      if (request_id) {
        SendMessageAck(request_id, std::move(ret.result));
      }
      return;
    }
  }
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

  if (*method == "dispatchProtocolMessage") {
    if (params.size() < 1) {
      return;
    }
    const std::string* protocol_message = params[0].GetIfString();
    if (!agent_host_ || !protocol_message) {
      return;
    }
#if BUILDFLAG(ARKWEB_DEVTOOLS)
    InterceptScriptExecutionDisabled(*protocol_message);
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

    if (ProtocolLoggingEnabled()) {
      LogProtocolMessage(ProtocolMessageType::METHOD, *protocol_message);
    }
    agent_host_->DispatchProtocolMessage(
        this, base::as_bytes(base::span(*protocol_message)));
  } else if (*method == "loadCompleted") {
#if BUILDFLAG(ARKWEB_DEVTOOLS)
    HandleLoadCompleted();
#else
    auto* rfh = web_contents()->GetPrimaryMainFrame();
    if (rfh == nullptr) {
      return;
    }
    rfh->ExecuteJavaScriptForTests(
        u"DevToolsAPI.setUseSoftMenu(true);", base::NullCallback(), 0);
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)
  } else if (*method == "loadNetworkResource") {
    if (params.size() < 3) {
      return;
    }

    // TODO(pfeldman): handle some of the embedder messages in content.
    const std::string* url = params[0].GetIfString();
    const std::string* headers = params[1].GetIfString();
    std::optional<const int> stream_id = params[2].GetIfInt();
    if (!url || !headers || !stream_id.has_value()) {
      return;
    }

    GURL gurl(*url);
    if (!gurl.is_valid()) {
      base::Value::Dict response;
      response.Set("statusCode", 404);
      response.Set("urlValid", false);
      SendMessageAck(request_id, std::move(response));
      return;
    }

    net::NetworkTrafficAnnotationTag traffic_annotation =
        net::DefineNetworkTrafficAnnotation(
            "devtools_handle_front_end_messages", R"(
            semantics {
              sender: "Developer Tools"
              description:
                "When user opens Developer Tools, the browser may fetch "
                "additional resources from the network to enrich the debugging "
                "experience (e.g. source map resources)."
              trigger: "User opens Developer Tools to debug a web page."
              data: "Any resources requested by Developer Tools."
              destination: OTHER
            }
            policy {
              cookies_allowed: YES
              cookies_store: "user"
              setting:
                "It's not possible to disable this feature from settings."
              chrome_policy {
                DeveloperToolsAvailability {
                  policy_options {mode: MANDATORY}
                  DeveloperToolsAvailability: 2
                }
              }
            })");

    // Based on DevToolsUIBindings::LoadNetworkResource.
    auto resource_request = std::make_unique<network::ResourceRequest>();
    resource_request->url = gurl;
    // TODO(caseq): this preserves behavior of URLFetcher-based
    // implementation. We really need to pass proper first party origin from
    // the front-end.
    resource_request->site_for_cookies = net::SiteForCookies::FromUrl(gurl);
    resource_request->headers.AddHeadersFromString(*headers);

    scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory;
    if (gurl.SchemeIsFile()) {
      mojo::PendingRemote<network::mojom::URLLoaderFactory> pending_remote =
          content::CreateFileURLLoaderFactory(
              base::FilePath() /* profile_path */,
              nullptr /* shared_cors_origin_access_list */);
      url_loader_factory = network::SharedURLLoaderFactory::Create(
          std::make_unique<network::WrapperPendingSharedURLLoaderFactory>(
              std::move(pending_remote)));
    } else if (content::HasWebUIScheme(gurl)) {
      base::Value::Dict response;
      response.Set("statusCode", 403);
      SendMessageAck(request_id, std::move(response));
      return;
    } else {
#if BUILDFLAG(ARKWEB_CRASHPAD)
      if (!inspected_contents_) {
        return;
      }
#endif
      auto* rfh = inspected_contents_->GetPrimaryMainFrame();
      if (rfh == nullptr) {
        return;
      }
      auto* partition = rfh->GetStoragePartition();
      url_loader_factory = partition->GetURLLoaderFactoryForBrowserProcess();
    }

    auto simple_url_loader = network::SimpleURLLoader::Create(
        std::move(resource_request), traffic_annotation);
    auto resource_loader = std::make_unique<NetworkResourceLoader>(
        *stream_id, this, std::move(simple_url_loader),
        url_loader_factory.get(), request_id);
    loaders_.insert(std::move(resource_loader));
    return;
  } else if (*method == "getPreferences") {
    SendMessageAck(request_id,
                   GetPrefs()->GetDict(prefs::kDevToolsPreferences).Clone());
    return;
  } else if (*method == "setPreference") {
    if (params.size() < 2) {
      return;
    }
    const std::string* name = params[0].GetIfString();

    // We're just setting params[1] as a value anyways, so just make sure it's
    // the type we want, but don't worry about getting it.
    if (!name || !params[1].is_string()) {
      return;
    }

    ScopedDictPrefUpdate update(GetPrefs(), prefs::kDevToolsPreferences);
    update->Set(*name, std::move(params[1]));
  } else if (*method == "removePreference") {
    const std::string* name = params[0].GetIfString();
    if (!name) {
      return;
    }
    ScopedDictPrefUpdate update(GetPrefs(), prefs::kDevToolsPreferences);
    update->Remove(*name);
  } else if (*method == "setOpenNewWindowForPopups") {
    // 作用：处理前端"自动为弹出窗口打开DevTools"设置项的开关消息
    // 合理性：类比Chrome的DevToolsWindow::SetOpenNewWindowForPopups，CEF层用成员标志位实现，构造时从PrefService恢复保证dock切换后不丢失
    // 必要性：不处理此消息则弹窗自动打开DevTools功能无法响应用户设置变更
    if (params.size() >= 1 && params[0].is_bool()) {
      // 作用：从消息参数读取开关值并更新成员标志；合理性：前端发送bool参数表示启用/禁用；必要性：标志位驱动MaybeAutoOpenDevToolsForPopup判断是否自动打开
      open_new_window_for_popups_ = params[0].GetBool();
    }
  } else if (*method == "requestFileSystems") {
#if BUILDFLAG(ARKWEB_DEVTOOLS)
    RequestFileSystems();
#else
    web_contents()->GetPrimaryMainFrame()->ExecuteJavaScriptForTests(
        u"DevToolsAPI.fileSystemsLoaded([]);", base::NullCallback(), 0);
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)
  } else if (*method == "reattach") {
    if (!agent_host_) {
      return;
    }
    agent_host_->DetachClient(this);
    agent_host_->AttachClient(this);
  } else if (*method == "registerExtensionsAPI") {
    if (params.size() < 2) {
      return;
    }
    const std::string* origin = params[0].GetIfString();
    const std::string* script = params[1].GetIfString();
    if (!origin || !script) {
      return;
    }
    extensions_api_[*origin + "/"] = *script;
  } else if (*method == "save") {
    if (params.size() < 3) {
      return;
    }
    const std::string* url = params[0].GetIfString();
    const std::string* content = params[1].GetIfString();
    std::optional<bool> save_as = params[2].GetIfBool();
    if (!url || !content || !save_as.has_value()) {
      return;
    }
    file_manager_.SaveToFile(*url, *content, *save_as);
  } else if (*method == "append") {
    if (params.size() < 2) {
      return;
    }
    const std::string* url = params[0].GetIfString();
    const std::string* content = params[1].GetIfString();
    if (!url || !content) {
      return;
    }
    file_manager_.AppendToFile(*url, *content);
#if BUILDFLAG(ARKWEB_DEVTOOLS)
  } else if (*method == "openInNewTab") {
    if (params.size() < 1) {
      return;
    }
    const std::string* url = params[0].GetIfString();
    if (url == nullptr) {
      return;
    }
    content::OpenURLParams urlParams(GURL(*url), content::Referrer(),
                                     WindowOpenDisposition::NEW_FOREGROUND_TAB,
                                     ui::PAGE_TRANSITION_LINK, false);
    if (inspected_contents_ != nullptr) {
      inspected_contents_->OpenURL(urlParams, {});
    } else {
      if (content::WebContentsImpl::GetAllWebContents().empty()) {
        return;
      }
      content::WebContents* contents = content::WebContentsImpl::GetAllWebContents().back();
      if (contents) {
        contents->OpenURL(urlParams, {});
      }
    }
    return;
  } else if (*method == "addFileSystem") {
    if (params.size() < 1) {
      return;
    }
    const std::string* type = params[0].GetIfString();
    if (!type) {
      return;
    }
    file_manager_.AddFileSystem(*type);
  } else if (*method == "removeFileSystem") {
    if (params.size() < 1) {
      return;
    }
    const std::string* file_system_path = params[0].GetIfString();
    if (!file_system_path) {
      return;
    }
    file_manager_.RemoveFileSystem(*file_system_path);
  } else if (*method == "inspectedURLChanged") {
    if (params.size() < 1) {
      return;
    }
    const std::string* url = params[0].GetIfString();
    if (!url) {
      return;
    }
    content::NavigationController& controller = web_contents()->GetController();
    content::NavigationEntry* entry = controller.GetActiveEntry();
    const std::string kHttpPrefix = "http://";
    const std::string kHttpsPrefix = "https://";
    const std::string simplified_url =
        base::StartsWith(*url, kHttpsPrefix, base::CompareCase::SENSITIVE)
            ? url->substr(kHttpsPrefix.length())
            : base::StartsWith(*url, kHttpPrefix, base::CompareCase::SENSITIVE)
                  ? url->substr(kHttpPrefix.length())
                  : *url;
    // DevTools UI is not localized.
    web_contents()->UpdateTitleForEntry(
        entry, base::UTF8ToUTF16(
                   base::StringPrintf(kTitleFormat, simplified_url.c_str())));

    return;
  } else if (*method == "getHostConfig") {
    SendMessageAck(request_id, base::Value::Dict());
    return;
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)
  } else {
    return;
  }

  if (request_id) {
    SendMessageAck(request_id, base::Value::Dict());
  }
}

void CefDevToolsFrontend::DispatchProtocolMessage(
    content::DevToolsAgentHost* agent_host,
    base::span<const uint8_t> message) {
  if (!frontend_browser_->GetWebContents() ||
      frontend_browser_->GetWebContents()->IsBeingDestroyed()) {
    return;
  }

  std::string_view str_message(reinterpret_cast<const char*>(message.data()),
                                message.size());
  if (ProtocolLoggingEnabled()) {
    // Quick check to avoid parsing the JSON object. Events begin with a
    // "method" value whereas method results begin with an "id" value.
    LogProtocolMessage(base::StartsWith(str_message, "{\"method\":")
                           ? ProtocolMessageType::EVENT
                           : ProtocolMessageType::RESULT,
                       str_message);
  }

  if (str_message.length() < kMaxMessageChunkSize) {
    CallClientFunction("DevToolsAPI", "dispatchMessage",
                       base::Value(std::string(str_message)));
  } else {
    size_t total_size = str_message.length();
    for (size_t pos = 0; pos < str_message.length();
         pos += kMaxMessageChunkSize) {
      std::string_view str_message_chunk =
          str_message.substr(pos, kMaxMessageChunkSize);

      CallClientFunction(
          "DevToolsAPI", "dispatchMessageChunk",
          base::Value(std::string(str_message_chunk)),
          base::Value(base::NumberToString(pos ? 0 : total_size)));
    }
  }
}

void CefDevToolsFrontend::CallClientFunction(
    const std::string& object_name,
    const std::string& method_name,
    base::Value arg1,
    base::Value arg2,
    base::Value arg3,
    base::OnceCallback<void(base::Value)> cb) {
  std::string javascript;
#if BUILDFLAG(ARKWEB_CRASHPAD)
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif
  auto* rfh = web_contents()->GetPrimaryMainFrame();
  if (rfh == nullptr) {
#if BUILDFLAG(ARKWEB_CRASHPAD)
    if (cb) {
      std::move(cb).Run(base::Value());
    }
#endif
    return;
  }

  rfh->AllowInjectingJavaScript();

  base::Value::List arguments;
  if (!arg1.is_none()) {
    arguments.Append(std::move(arg1));
    if (!arg2.is_none()) {
      arguments.Append(std::move(arg2));
      if (!arg3.is_none()) {
        arguments.Append(std::move(arg3));
      }
    }
  }
  rfh->ExecuteJavaScriptMethod(
      base::ASCIIToUTF16(object_name), base::ASCIIToUTF16(method_name),
      std::move(arguments), std::move(cb));
}

void CefDevToolsFrontend::SendMessageAck(int request_id,
                                         base::Value::Dict arg) {
  CallClientFunction("DevToolsAPI", "embedderMessageAck",
                     base::Value(request_id), base::Value(std::move(arg)));
}

bool CefDevToolsFrontend::ProtocolLoggingEnabled() const {
  return !protocol_log_file_.empty();
}

void CefDevToolsFrontend::LogProtocolMessage(ProtocolMessageType type,
                                             const std::string_view& message) {
  DCHECK(ProtocolLoggingEnabled());

  std::string to_log(message.substr(0, kMaxLogLineLength));

  // Execute in an ordered context that allows blocking.
  auto task_runner = CefTaskRunnerManager::Get()->GetBackgroundTaskRunner();
#if BUILDFLAG(ARKWEB_CRASHPAD)
  base::FilePath log_file = protocol_log_file_;
  task_runner->PostTask(
      FROM_HERE, base::BindOnce(::LogProtocolMessage, std::move(log_file), type,
                                std::move(to_log)));
#else
  task_runner->PostTask(
    FROM_HERE, base::BindOnce(::LogProtocolMessage, protocol_log_file_, type,
                              std::move(to_log)));
#endif

}

void CefDevToolsFrontend::AgentHostClosed(
    content::DevToolsAgentHost* agent_host) {
  DCHECK(agent_host == agent_host_.get());
  agent_host_ = nullptr;
  Close();
}

PrefService* CefDevToolsFrontend::GetPrefs() const {
#if BUILDFLAG(ARKWEB_CRASHPAD)
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif
  auto* web_contents = frontend_browser_->web_contents();
  if (web_contents == nullptr) {
    return nullptr;
  }
  auto* browser_context = CefBrowserContext::FromBrowserContext(web_contents->GetBrowserContext());
  if (browser_context == nullptr) {
    return nullptr;
  }
  return browser_context->AsProfile()->GetPrefs();
}

#if BUILDFLAG(ARKWEB_DEVTOOLS)
// 作用：CEF层拦截setScriptExecutionDisabled同步WebContentsImpl状态
// 合理性：从HandleMessageFromDevToolsFrontend中提取以降低嵌套深度
// 必要性：前端可能通过JSON消息路径直接发送CDP命令，此拦截是唯一的browser端状态同步点
void CefDevToolsFrontend::InterceptScriptExecutionDisabled(
    const std::string& protocol_message) {
  if (protocol_message.find("Emulation.setScriptExecutionDisabled") ==
      std::string::npos) {
    return;
  }
  auto parsed = base::JSONReader::Read(
      protocol_message, base::JSON_PARSE_CHROMIUM_EXTENSIONS);
  if (!parsed || !parsed->is_dict()) {
    return;
  }
  const std::string* cdp_method = parsed->GetDict().FindString("method");
  if (!cdp_method || *cdp_method != "Emulation.setScriptExecutionDisabled") {
    return;
  }
  bool disabled = false;
  const base::Value::Dict* cdp_params = parsed->GetDict().FindDict("params");
  if (cdp_params) {
    std::optional<bool> value = cdp_params->FindBool("value");
    if (value.has_value()) {
      disabled = value.value();
    }
  }
  if (!inspected_contents_) {
    return;
  }
  auto* wc_impl = static_cast<content::WebContentsImpl*>(
      inspected_contents_.get());
  if (wc_impl) {
    wc_impl->SetCdpScriptExecutionDisabled(disabled);
  }
}

// 作用：处理loadCompleted前端消息
// 合理性：从HandleMessageFromDevToolsFrontend中提取以降低嵌套深度
// 必要性：保持原有setInitialTargetId+extensions+setUseSoftMenu逻辑不变
void CefDevToolsFrontend::HandleLoadCompleted() {
  // 作用：通过JS注入设置前端初始targetId
  // 合理性：ArkWeb前端URL不含targetType=tab参数，需在loadCompleted时通过API注入
  // 必要性：无targetId则前端CDP命令路由失败，Settings等功能不工作
  if (agent_host_) {
    CallClientFunction("DevToolsAPI", "setInitialTargetId",
                       base::Value(agent_host_->GetId()));
  }
#if BUILDFLAG(ARKWEB_ARKWEB_EXTENSIONS)
  if (inspected_contents_) {
#if BUILDFLAG(ARKWEB_CRASHPAD)
    int tab_id = cef::GetTabIdForWebContents(inspected_contents_.get());
#else
    int tab_id = cef::GetTabIdForWebContents(inspected_contents_);
#endif
    CallClientFunction("DevToolsAPI", "setInspectedTabId",
                       base::Value(tab_id));
    AddDevToolsExtensionsToClient();
  } else {
    LOG(ERROR) << " func:" << __func__
               << " inspected_contents_ is null, skip extensions block.";
  }
#endif // BUILDFLAG(ARKWEB_ARKWEB_EXTENSIONS)

  auto* rfh = web_contents()->GetPrimaryMainFrame();
  if (rfh == nullptr) {
    return;
  }
  LOG(INFO) << "CefDevToolsFrontend::HandleLoadCompleted show_toolbox_: "
            << show_toolbox_;
  if (!show_toolbox_) {
    rfh->ExecuteJavaScriptForTests(
        u"DevToolsAPI.setUseSoftMenu(true);", base::NullCallback(), 0);
  }
}

void CefDevToolsFrontend::SetSourceTargetId(const CefString& source_id,
                                            const CefString& target_id) {
  source_id_ = source_id;
  target_id_ = target_id;
}

AlloyBrowserHostImpl* CefDevToolsFrontend::GetFrontendBrowser() {
  return frontend_browser_.get();
}

void CefDevToolsFrontend::FileSystemAdded(
    const std::string& error,
    const CefDevToolsFileManager::FileSystem* file_system) {
  if (file_system) {
    CallClientFunction("DevToolsAPI", "fileSystemAdded", base::Value(error),
        base::Value(CreateFileSystemValue(*file_system)));
  } else {
    CallClientFunction("DevToolsAPI", "fileSystemAdded", base::Value(error));
  }
}

void CefDevToolsFrontend::FileSystemRemoved(const std::string& file_system_path) {
  CallClientFunction("DevToolsAPI", "fileSystemRemoved",
      base::Value(file_system_path));
}

void CefDevToolsFrontend::FilePathsChanged(
    const std::vector<std::string>& changed_paths,
    const std::vector<std::string>& added_paths,
    const std::vector<std::string>& removed_paths) {
  const int kMaxPathsPerMessage = 1000;
  size_t changed_index = 0;
  size_t added_index = 0;
  size_t removed_index = 0;
  // Dispatch limited amount of file paths in a time to avoid
  // IPC max message size limit. See https://crbug.com/797817.
  while (changed_index < changed_paths.size() ||
         added_index < added_paths.size() ||
         removed_index < removed_paths.size()) {
    int budget = kMaxPathsPerMessage;
    base::Value::List changed;
    base::Value::List added;
    base::Value::List removed;
    while (budget > 0 && changed_index < changed_paths.size()) {
      changed.Append(changed_paths[changed_index++]);
      --budget;
    }
    while (budget > 0 && added_index < added_paths.size()) {
      added.Append(added_paths[added_index++]);
      --budget;
    }
    while (budget > 0 && removed_index < removed_paths.size()) {
      removed.Append(removed_paths[removed_index++]);
      --budget;
    }
    CallClientFunction("DevToolsAPI", "fileSystemFilesChangedAddedRemoved",
                     base::Value(std::move(changed)),
                     base::Value(std::move(added)),
                     base::Value(std::move(removed)));
  }
}

void CefDevToolsFrontend::SendMessageAck(int request_id,
                                         base::Value arg) {
  CallClientFunction("DevToolsAPI", "embedderMessageAck",
                     base::Value(request_id), std::move(arg));
}

void CefDevToolsFrontend::RequestFileSystems() {
  base::Value::List file_systems_value;
  for (auto const& file_system : file_manager_.GetFileSystems()) {
    file_systems_value.Append(CreateFileSystemValue(file_system));
  }
  CallClientFunction("DevToolsAPI", "fileSystemsLoaded",
      base::Value(std::move(file_systems_value)));
}

bool CefDevToolsFrontend::ShouldUseTabTarget() {
#if BUILDFLAG(ARKWEB_CRASHPAD)
  if (!inspected_contents_) {
    LOG(ERROR) << " func:" << __func__ << " inspected_contents_ is null.";
    return false;
  }
#endif // ARKWEB_DEVTOOLS || ARKWEB_CRASHPAD
  if (inspected_contents_->GetURL().SchemeIs("chrome-extension")) {
    return false;
  }

#if BUILDFLAG(ARKWEB_CRASHPAD)
  scoped_refptr<content::DevToolsAgentHost> host = content::DevToolsAgentHost::GetForTab(inspected_contents_.get());
#else
  scoped_refptr<content::DevToolsAgentHost> host = content::DevToolsAgentHost::GetForTab(inspected_contents_);
#endif
  if (host && host->GetType() == content::DevToolsAgentHost::kTypeTab) {
    return true;
  }

  if (!inspected_contents_->GetOuterWebContents() && inspected_contents_->GetPrimaryMainFrame()) {
    return true;
  }
  return false;
}
// static
// 作用：在活跃实例集合中查找正在调试指定WebContents的CefDevToolsFrontend实例
// 合理性：类比Chrome的GetInstanceForInspectedWebContents，线性遍历全局集合匹配inspected_contents_指针
// 必要性：弹出窗口自动打开DevTools需通过opener链反查DevTools实例，无此方法无法建立opener→DevTools的映射
CefDevToolsFrontend* CefDevToolsFrontend::FindByInspectedContents(
    content::WebContents* contents) {
  if (!contents) {
    return nullptr;
  }
  for (auto* instance : GetLiveInstances()) {
#if BUILDFLAG(ARKWEB_CRASHPAD)
    if (instance->inspected_contents_ &&
        instance->inspected_contents_.get() == contents) {
      return instance;
    }
#else
    if (instance->inspected_contents_ == contents) {
      return instance;
    }
#endif
  }
  return nullptr;
}

// 作用：判断新创建的弹出窗口是否应自动打开DevTools并触发打开流程
// 合理性：遍历opener链查找启用了"auto-open for popups"的DevTools实例，类比Chrome的DevToolsWindow弹窗自动打开逻辑
// 必要性：这是弹窗自动打开DevTools的核心入口，不实现则"自动附加到新页面"设置对弹窗无效
void CefDevToolsFrontend::MaybeAutoOpenDevToolsForPopup(
    content::WebContents* popup_contents) {
  if (!popup_contents || FindByInspectedContents(popup_contents)) {
    return;
  }

  // 路径1：沿opener链查找启用了自动打开标志的DevTools实例
  content::WebContents* opener = FindAutoOpenOpenerInChain(popup_contents);
  if (opener) {
    TriggerAutoOpenForPopup(popup_contents,
                            BuildAutoOpenParams(opener, popup_contents));
    return;
  }

  // 路径2：从PrefService读取auto-attach偏好回退
  content::WebContents* direct_opener =
      popup_contents->HasLiveOriginalOpenerChain()
          ? popup_contents->GetFirstWebContentsInLiveOriginalOpenerChain()
          : nullptr;
  if (direct_opener && ReadAutoAttachFromPrefs(direct_opener)) {
    TriggerAutoOpenForPopup(popup_contents,
                            BuildAutoOpenParams(direct_opener, popup_contents));
  }
}

// 作用：沿opener链查找启用了自动打开标志的opener WebContents
// 合理性：遍历opener链查找open_new_window_for_popups_为true的DevTools实例对应的被调试页面
// 必要性：MaybeAutoOpenDevToolsForPopup的opener链遍历逻辑提取为独立方法以降低圈复杂度和嵌套深度
content::WebContents* CefDevToolsFrontend::FindAutoOpenOpenerInChain(
    content::WebContents* popup_contents) {
  content::WebContents* current = popup_contents;
  while (current->HasLiveOriginalOpenerChain()) {
    content::WebContents* opener =
        current->GetFirstWebContentsInLiveOriginalOpenerChain();
    if (!opener) break;
    CefDevToolsFrontend* frontend = FindByInspectedContents(opener);
    if (frontend && frontend->open_new_window_for_popups()) {
      return opener;
    }
    current = opener;
  }
  return nullptr;
}

// 作用：从PrefService读取auto-attach-to-created-pages偏好
// 合理性：opener链未命中时回退到持久化偏好读取，覆盖三个字典
// 必要性：dock模式切换重建CefDevToolsFrontend后FindByInspectedContents可能失败，偏好回退是必要兜底
bool CefDevToolsFrontend::ReadAutoAttachFromPrefs(
    content::WebContents* opener) {
  auto* browser_context = opener->GetBrowserContext();
  if (!browser_context) return false;
  auto* cef_bc = CefBrowserContext::FromBrowserContext(browser_context);
  if (!cef_bc) return false;
  auto* prefs = cef_bc->AsProfile()->GetPrefs();
  if (!prefs) return false;

  auto read_bool = [](const base::Value::Dict& dict,
                      const char* key) -> bool {
    auto* val = dict.Find(key);
    return val && val->is_bool() && val->GetBool();
  };
  return read_bool(prefs->GetDict(prefs::kDevToolsPreferences),
                   "auto-attach-to-created-pages") ||
         read_bool(prefs->GetDict(
                       prefs::kDevToolsSyncedPreferencesSyncEnabled),
                   "auto-attach-to-created-pages") ||
         read_bool(prefs->GetDict(
                       prefs::kDevToolsSyncedPreferencesSyncDisabled),
                   "auto-attach-to-created-pages");
}

// 作用：构造自动打开DevTools的RequestOpenDevToolsParams
// 合理性：opener链命中和偏好回退两条路径都需构造相同参数，提取为公共方法避免重复
// 必要性：两处重复代码提取后降低nbnc行数
RequestOpenDevToolsParams CefDevToolsFrontend::BuildAutoOpenParams(
    content::WebContents* opener, content::WebContents* popup) {
  RequestOpenDevToolsParams params;
  params.type = "auto";
  if (auto host = content::DevToolsAgentHost::GetOrCreateForTab(opener)) {
    params.source_id = host->GetId();
  }
  if (auto host = content::DevToolsAgentHost::GetOrCreateForTab(popup)) {
    params.target_id = host->GetId();
  }
  return params;
}

// 作用：通过per-NWeb V2回调路径为弹窗触发自动打开DevTools，失败时回退到静态回调，并安排延迟重试
// 合理性：优先走AlloyBrowserHostImplExt→contents_delegate→PcWebPageController路径以保留docking上下文，静态回调路径会丢失docking模式导致停靠模式打开为独立窗口
// 必要性：不使用per-NWeb路径则docked模式弹窗自动打开的DevTools无法正确停靠
void CefDevToolsFrontend::TriggerAutoOpenForPopup(
    content::WebContents* popup_contents,
    const RequestOpenDevToolsParams& params) {
  // Try per-NWeb V2 callback path first (preserves docking context)
  auto browser = AlloyBrowserHostImpl::GetBrowserForContents(popup_contents);
  auto* ext = browser ? browser->AsAlloyBrowserHostImplExt().get() : nullptr;
  if (ext) {
    ext->OnRequestOpenDevTools(const_cast<RequestOpenDevToolsParams*>(&params));
  } else {
    RequestOpenDevToolsHandler::GetInstance()->OnRequestOpenDevTools(params);
  }

  // 作用：无条件安排1000ms延迟重试以解决PcWebPageController异步注册的竞态条件
  // 合理性：contents_delegate()存在不代表内部回调已注册，首次调用可能静默失败，1000ms后通常已注册完成
  // 必要性：不重试则竞态条件下弹窗自动打开静默失败
  base::WeakPtr<content::WebContents> popup_ref = popup_contents->GetWeakPtr();
  RequestOpenDevToolsParams retry_params = params;
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> popup,
             RequestOpenDevToolsParams p) {
            auto* popup_contents = popup.get();
            if (!popup_contents) return;
            // 作用：重试前检查弹窗是否已有DevTools实例；合理性：FindByInspectedContents反查活跃实例集合；必要性：避免首次调用成功后重复打开
            if (FindByInspectedContents(popup_contents)) {
              return;
            }
            // Retry with per-NWeb V2 callback path
            auto browser = AlloyBrowserHostImpl::GetBrowserForContents(popup_contents);
            auto* ext = browser ? browser->AsAlloyBrowserHostImplExt().get() : nullptr;
            if (ext) {
              ext->OnRequestOpenDevTools(&p);
            } else {
              RequestOpenDevToolsHandler::GetInstance()->OnRequestOpenDevTools(p);
            }
          },
          popup_ref, std::move(retry_params)),
      base::Milliseconds(1000));
}
#endif // BUILDFLAG(ARKWEB_DEVTOOLS)

#if BUILDFLAG(ARKWEB_ARKWEB_EXTENSIONS)
base::Value::Dict CefDevToolsFrontend::BuildExtensionInfo(
    const extensions::Extension* extension) {
  GURL url =
      extensions::chrome_manifest_urls::GetDevToolsPage(extension);
  const bool is_extension_url =
      (url.SchemeIs(extensions::kExtensionScheme)
       || url.SchemeIs(extensions::kArkwebExtensionScheme)) &&
      url.host() == extension->id();
  DCHECK(is_extension_url || url.SchemeIsHTTPOrHTTPS());
#if BUILDFLAG(ARKWEB_CRASHPAD)
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif
  // Each devtools extension will need to be able to run in the devtools
  // process. Grant the devtools process the ability to request URLs from the
  // extension.
  content::ChildProcessSecurityPolicy::GetInstance()->GrantRequestOrigin(
      web_contents()->GetPrimaryMainFrame()->GetProcess()->GetID().GetUnsafeValue(),
      url::Origin::Create(extension->url()));

  extensions::ExtensionManagement* management =
      extensions::ExtensionManagementFactory::GetForBrowserContext(
          web_contents()->GetBrowserContext());
  base::Value::List runtime_allowed_hosts;
  std::vector<std::string> allowed_hosts =
      management->GetPolicyAllowedHosts(extension).ToStringVector();
  for (auto& host : allowed_hosts) {
    runtime_allowed_hosts.Append(std::move(host));
  }
  base::Value::List runtime_blocked_hosts;
  std::vector<std::string> blocked_hosts =
      management->GetPolicyBlockedHosts(extension).ToStringVector();
  for (auto& host : blocked_hosts) {
    runtime_blocked_hosts.Append(std::move(host));
  }

  base::Value::Dict extension_info;
  extension_info.Set("startPage", url.spec());
  extension_info.Set("name", extension->name());
  extension_info.Set("exposeExperimentalAPIs",
                     extension->permissions_data()->HasAPIPermission(
                         extensions::mojom::APIPermissionID::kExperimental));
  extension_info.Set("allowFileAccess", extensions::util::AllowFileAccess(
                                            extension->id(),
                                            web_contents()->GetBrowserContext()));
  extension_info.Set(
      "hostsPolicy",
      base::Value::Dict()
          .Set("runtimeAllowedHosts", std::move(runtime_allowed_hosts))
          .Set("runtimeBlockedHosts", std::move(runtime_blocked_hosts)));
  return extension_info;
}

void CefDevToolsFrontend::AddDevToolsExtensionsToClient() {
  // See DevToolsUIBindings::AddDevToolsExtensionsToClient.
#if BUILDFLAG(ARKWEB_CRASHPAD)
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
#endif
  const extensions::ExtensionRegistry* registry =
      extensions::ExtensionRegistry::Get(web_contents()->GetBrowserContext());
  if (!registry) {
    return;
  }

  base::Value::List results;
  base::Value::List forbidden_origins;
  for (const scoped_refptr<const extensions::Extension>& extension :
       registry->enabled_extensions()) {
    if (extensions::Manifest::IsComponentLocation(extension->location())) {
      forbidden_origins.Append(extension->origin().Serialize());
    }
    if (extensions::chrome_manifest_urls::GetDevToolsPage(extension.get())
            .is_empty()) {
      continue;
    }
    results.Append(BuildExtensionInfo(extension.get()));
  }


  CallClientFunction("DevToolsAPI", "setOriginsForbiddenForExtensions",
                     base::Value(std::move(forbidden_origins)));
  CallClientFunction("DevToolsAPI", "addExtensions",
                     base::Value(std::move(results)));
}
#endif // BUILDFLAG(ARKWEB_ARKWEB_EXTENSIONS)
