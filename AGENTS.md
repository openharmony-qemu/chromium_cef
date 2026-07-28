# Chromium CEF (Chromium Embedded Framework) 指引

## 项目定位

本仓库是 CEF（Chromium Embedded Framework）的 OpenHarmony 适配版本。CEF 基于 Chromium content 层封装，为宿主应用提供嵌入式浏览器能力。本仓库在上游 CEF 基础上新增 `ohos_cef_ext/` 目录，承载 OpenHarmony 平台扩展（ArkWeb 集成、安全浏览、广告过滤、JS Bridge 等）。

## 代码地图

### 上游 CEF 核心（不应修改）

```
include/          CEF 公共 API 定义
  cef_browser.h   浏览器生命周期和操作接口（含 SendDevToolsMessage / ExecuteDevToolsMethod）
  cef_client.h    客户端回调注册入口（handler 集合）
  cef_frame.h     帧（frame）操作接口
  cef_v8.h        V8 JS 交互接口
  views/          Views 框架（原生 UI 控件）
  wrapper/        辅助工具类（helpers、message_router、resource_manager）
  capi/           C API 定义
  internal/       平台相关内部类型
  base/           基础类型库（callback、weak_ptr、ref_counted 等）
libcef/           CEF 实现（编译进 libcef.so）
  browser/        浏览器进程实现（context、browser_host、download、devtools 等）
  common/         多进程共享代码（switches、values、request、response 等）
  renderer/       渲染进程实现（DOM、V8、frame observer、blink_glue）
libcef_dll/       C→C++ 桥接层（由 translator.py 自动生成）
  cpptoc/         C++ → C 转换（宿主调用 CEF 实现的桥）
  ctocpp/         C → C++ 转换（CEF 回调宿主的桥）
  wrapper/        高级封装（message_router、resource_manager 等）
```

### OpenHarmony 扩展（本仓库维护重点）

```
ohos_cef_ext/
  include/                OpenHarmony 扩展接口定义
    arkweb_browser_ext.h  浏览器扩展接口
    arkweb_client_ext.h   客户端扩展回调
    arkweb_frame_ext.h    帧扩展接口
    cef_form_handler.h    表单处理
    cef_media_handler.h   媒体播放器
    cef_web_storage.h     Web 存储
    ...                   权限、下载、安全浏览等
  libcef/
    browser/              浏览器进程扩展
      alloy/              Alloy 模式扩展（browser_host_impl_ext）
      devtools/           DevTools 扩展（arkweb/ 下含消息处理和前端）
      javascript/         JS Bridge（oh_gin_javascript_bridge_*）
      net/                网络扩展（applink、url_rewrite、extra_headers）
      osr/                离屏渲染扩展
      permission/         权限管理
      ohos_safe_browsing/ 安全浏览
      adblock/            广告过滤
      anti_tracking/      反追踪
      ...                 下载、媒体、UA、全局配置等
    common/               多进程共享扩展
      mojom/              Mojo IPC 定义
      javascript/         JS Bridge 公共类型
    renderer/             渲染进程扩展
      javascript/         JS Bridge 渲染端
      extensions/         扩展渲染支持
```

### 测试和工具

```
tests/
  cefsimple/       最小浏览器示例（学习入口）
  cefclient/       完整功能演示
  ceftests/        gtest 自动化测试
  shared/          测试共享工具
tools/
  translator.py    cpptoc/ctocpp 代码生成器
  make_*.py        API/版本头文件生成
  patch.sh         Chromium 补丁管理
patch/
  patch.cfg        补丁配置（每个补丁条目含 name/path/condition）
  patches/         补丁文件（通过 cef_create_projects.sh 应用）
```

## 分层架构

```
宿主应用 (ArkTS/C++/Java)
  ↓ ohos_interface / ohos_nweb (web_webview 仓库)
  ↓
ArkWebCore.hap (Chromium + CEF 合编产物)
  ├── libcef/  (CEF 核心)
  │     ↓ include/ (CEF 公共 API)
  │     ↓ libcef_dll/ (C↔C++ 桥接，自动生成)
  │     ↓ libcef/browser|common|renderer (CEF 实现)
  │
  └── ohos_cef_ext/ (OpenHarmony 扩展)
        ↓ include/ (扩展接口)
        ↓ libcef/browser|common|renderer (扩展实现)
        ↓ 编译条件：is_arkweb_ext GN 变量控制
```

### 子目录专属指引（进入这些目录先读对应文件）

| 目录 | 指引文件 | 覆盖内容 |
|------|----------|----------|
| `tests/cefclient/` | `tests/cefclient/CLAUDE.md` | CEFClient 示例应用的 CMake 构建与集成模式 |
| `tests/cefsimple_capi/` | `tests/cefsimple_capi/CLAUDE.md` | 纯 C（C11）C API 示例，手动引用计数约定 |
| `tools/claude/` | `tools/claude/CLAUDE.md` | 上游 Chromium/GN·Ninja 构建背景 |

## 知识索引

**编辑前先声明（强约束）**：在动手改代码前，先用一句话说明——(1) 任务类别（API/桥接/补丁/扩展/渲染/权限/安全浏览/DevTools 等）；(2) 已读的深度文档；(3) 命中的硬约束与依赖方向禁止项。三者缺一律不得开始编辑；该声明须原样出现在最终回复的开头（见“最终回复要求”），供审阅核对。

### 按场景路由

改动前按场景定位，以下场景需要特别注意隐含约束：

| 场景 | 注意事项 |
|------|----------|
| 新增 OH 扩展源文件 | 必须在 `cef_arkweb_extension.gni` 中注册，否则不参与编译 |
| Chromium 补丁 | 通过 `patch/patch.cfg` 管理，由 `cef_create_projects.sh` 应用，最小化修改 |
| C↔C++ 桥接 | `libcef_dll/cpptoc|ctocpp/` 由 `tools/translator.py` 生成，禁止手改 |
| 新增 OH 扩展接口 | 在 `ohos_cef_ext/include/` 中以 C++ 虚类定义即可，**无需**修改桥接层。详见 [bridge-layer-analysis.md](docs/knowledge/bridge-layer-analysis.md) |

### 按路径路由

| 改动路径 | 先读 / 先做 |
|----------|-------------|
| `include/` 公共 API | 属上游代码，改动须走 `patch/`；改签名后必须运行 `./cef_create_projects.sh` 重新生成 `libcef_dll/` 桥接 |
| `libcef_dll/cpptoc|ctocpp/` | 生成产物，禁止手改；改上游头文件后由脚本重生成 |
| `ohos_cef_ext/**` | 读 [bridge-layer-analysis.md](docs/knowledge/bridge-layer-analysis.md)；新增源文件同步登记 `cef_arkweb_extension.gni` |
| `ohos_cef_ext/**/devtools/` | CDP 协议改动，遵守异步回调约定，勿用 `Runtime.evaluate` 操控页面 |
| `ohos_cef_ext/**/renderer/` | 仅此处可用 V8/DOM；跨进程数据须走 `common/mojom/` 的 Mojo IPC |
| `patch/patches/` | 读 `patch/patch.cfg` 的 name/path/condition 约定，改动最小化 |

### 按术语路由（任务描述/日志/接口中出现下列词时）

| 术语 | 先读 / 定位 |
|------|-------------|
| CDP / DevTools / SendDevToolsMessage | `ohos_cef_ext/libcef/browser/devtools/arkweb/`、`include/cef_browser.h`；结果经 `CefDevToolsMessageObserver` 异步回调 |
| JS Bridge / gin | `ohos_cef_ext/{libcef/browser,common,renderer}/javascript/` |
| Mojo / IPC | `ohos_cef_ext/libcef/common/mojom/` |
| `_for_include` / `_ext` | 见下方术语表与反模式；勿忽略 include 注入文件 |
| Alloy / OSR | 见下方术语表；OSR 相关在 `ohos_cef_ext/libcef/browser/osr/` |

### 深度知识文档

| 文档 | 何时读 |
|------|--------|
| [bridge-layer-analysis.md](docs/knowledge/bridge-layer-analysis.md) | 新增/修改 OH 扩展接口，或纠结“是否需要动桥接层”时；含 C↔C++ 桥接层设计目的与 ArkWeb 绕过桥接层的原因 |

## 专家经验与约束

### 硬约束

- `include/` 和 `libcef/` 是上游 CEF 代码，**不要直接修改**。修改必须通过 `patch/` 目录提交补丁
- `libcef_dll/cpptoc/` 和 `ctocpp/` 由 `tools/translator.py` 自动生成，**禁止手动编辑**
- OpenHarmony 扩展代码只放在 `ohos_cef_ext/` 目录内
- 新增 OH 扩展源文件必须在 `cef_arkweb_extension.gni` 中注册
- CDP 调用（SendDevToolsMessage）是异步的，结果通过 CefDevToolsMessageObserver 回调
- V8/DOM 操作只能在渲染进程中使用

### 询问后再动（需先确认/升级）

- 修改 `ohos_cef_ext/include/` 中**已发布**扩展接口的签名、语义、生命周期或错误码 → 属破坏性变更，先评估对宿主（web_webview / ohos_nweb）的兼容性影响，并升级相应版本号；不确定时先询问，不要直接改
- 触及权限、安全浏览、反追踪、鉴权信任边界（`permission/`、`ohos_safe_browsing/`、`anti_tracking/`）→ 先确认再改，勿放宽默认策略
- 改动持久化数据格式、序列化、Mojo IPC 消息结构 → 需保证跨版本兼容，先确认
- 新增/升级第三方依赖或涉及 License 的改动 → 先询问
- DFX/日志：不要删改既有日志 tag、错误码文案与崩溃/故障归因信息（安全浏览、崩溃上报等依赖其做故障定位）；新增日志沿用所在模块既有 tag 规范，勿泄露 URL、cookie、token 等敏感数据

### 依赖方向禁止

- ❌ `ohos_cef_ext/` 代码不应直接 include `libcef/` 内部头文件（使用 `include/` 公共 API）
- ❌ 浏览器进程不能直接调用渲染进程对象（必须通过 Mojo IPC）
- ❌ `include/` 接口类不应依赖 `ohos_cef_ext/` 类型

### 反模式

- 不要在 AlloyBrowserHostImpl 继承链中添加虚方法，应使用 `ohos_cef_ext/` 的 `_ext` 后缀类扩展
- 不要用 Runtime.evaluate 做 JS 注入操控页面，应走 CDP 协议
- 不要忽略 `*_for_include.cc` 文件——它们通过 include 合并方式注入到主实现类中

### 领域术语表

| 术语 | 含义 |
|------|------|
| Alloy 模式 | CEF 两种运行模式之一（Alloy / Chrome），Alloy 无 Chrome 外壳，适合嵌入式 |
| OSR | Off-Screen Rendering，离屏渲染 |
| cpptoc / ctocpp | C++→C / C→C++ 桥接层，由 translator.py 自动生成 |
| Handler 模式 | CEF 通过 CefClient 注册各种 Handler 回调 |
| Frame | 页面帧，主 Frame = 主页面，子 Frame = iframe |
| _ext 后缀 | OpenHarmony 扩展文件命名约定 |
| _for_include | 编译时 include 注入文件，扩展上游类而不修改源码 |
| Mojo IPC | Chromium 进程间通信机制 |

## 编译和测试

本仓库不独立编译，跟随 `chromium_src` 的 OH 全量编译流程。

```sh
# 全量编译（从源码根目录）
./build.sh -t w -A rk3568

# 如果仅修改了 CEF，可以增量编译
./build.sh -A rk3568
```

提交使用 `git commit -s`，保留 DCO Signed-off-by 签名。

## 验证闭环

改完必须自证。按改动类型选择对应检查，**不要**只凭“看起来对了”收尾。

### 最小检查（任何改动都要过）

| 检查 | 命令 / 动作 |
|------|-------------|
| 增量编译 | `./build.sh -A rk3568`（源码根目录）；改动大或涉及依赖用全量 `./build.sh -t w -A rk3568` |
| 代码风格 | `tools/fix_style.sh`（clang-format 风格，勿手工对齐） |
| 提交签名 | `git commit -s`，保留 DCO Signed-off-by |

### 按任务追加检查

| 任务类型 | 追加动作 |
|----------|----------|
| 改了 `include/` 公共 API 签名 | 运行 `./cef_create_projects.sh` 重新生成 `libcef_dll/cpptoc|ctocpp/`，确认生成结果一并提交且未手改 |
| 新增 `ohos_cef_ext/` 源文件 | 确认已登记 `cef_arkweb_extension.gni`，且该文件确实参与编译（编译日志可见） |
| 改了 Chromium 补丁 | 确认 `patch/patch.cfg` 条目正确，`cef_create_projects.sh` 应用无冲突 |
| 改了功能逻辑 | 运行相关 `tests/ceftests` 用例：`ninja -C out/<build_dir> ceftests` 后执行对应单测（见 `tests/ceftests/*_unittest.cc`）。`<build_dir>` 取 GN 输出目录，本地 CEF 构建形如 `Release_GN_x64` / `Debug_GN_x64`；OH 合编时取 `chromium_src` 全量编译的产物目录 |
| 改了破坏性/兼容性敏感项 | 对照“询问后再动”清单复核，说明兼容性影响 |

### 完成定义（Done）

- 目标改动生效，且增量编译通过、`fix_style.sh` 无残留改动；
- 相关 `ceftests` 用例通过（若适用）；
- 未违反任一硬约束与依赖方向禁止项；生成产物已重生成而非手改；
- 若触及 API/权限/兼容性，已在回复中说明影响。

### 最终回复要求

结尾用简短列表报告：开头原样附上“编辑前先声明”的三项（任务类别 / 已读文档 / 命中约束）；随后列出改了哪些文件、跑了哪些检查及结果、是否有未验证项及原因。测试失败要如实贴出，不要粉饰。

### 无法在本仓库验证时的兜底

本仓库不独立编译/跑测。若无法在当前环境完成上述检查，**不得**声称已验证——改为：列出改动文件与预期影响、说明为何无法验证、请求在 `chromium_src` 全量环境中编译并运行对应 `ceftests`。
