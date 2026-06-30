# C↔C++ 桥接层分析

## 桥接层概述

`libcef_dll/` 目录包含 CEF 的 C↔C++ 桥接层，由 `tools/translator.py` 自动生成，**禁止手动编辑**。

| 子目录 | 职责 |
|--------|------|
| `cpptoc/` | C++→C 转换：将 C++ 对象包装为带函数指针的 C 结构体，供外部 C 调用方使用 |
| `ctocpp/` | C→C++ 转换：将 C 结构体回调解包为 C++ 虚函数调用 |
| `wrapper/` | 高级封装（message_router、resource_manager 等） |

核心模板类：
- `CefCppToCRefCounted`（`cpptoc_ref_counted.h`）：将 C++ 实现对象包装为 C 结构体传出 DLL 边界
- `CefCToCppRefCounted`（`ctocpp_ref_counted.h`）：将外部传入的 C 结构体包装为 C++ 对象在内部使用

## 桥接层的设计目的

解决 **DLL 跨模块 C ABI 边界** 问题：

```
宿主应用 (.exe/.dll)          libcef.dll
  C++ 调用方                    CEF 实现方
  ──────────                   ──────────
  CefCToCpp ────── C struct ────→ CefCppToC
  (ctocpp)        函数指针         (cpptoc)
                  回调
```

适用场景：外部宿主应用通过 C API（`include/capi/` 中的 `_cef_xxx_t` 函数指针结构体）链接 `libcef.dll`，需要跨越 DLL 边界的 ABI 兼容。

## ArkWeb 扩展是否使用桥接层

**否。** ArkWeb 扩展（`ohos_cef_ext/`）完全绕过了桥接层，原因如下：

1. **无 C ABI 边界**：CEF 核心代码与 OH 扩展合编到同一个 `libcef.so`（编译进同一个 HAP），不存在 DLL 边界
2. **纯 C++ 接口**：`ohos_cef_ext/include/` 中所有扩展接口都是 C++ 虚类（如 `ArkWebBrowserHostExt`、`ArkWebClientExt`），没有定义任何带 `CEF_CALLBACK` 函数指针的 C API 结构体
3. **零交叉引用**：在 `libcef_dll/` 中搜索不到任何 `ArkWeb`/`ohos_cef_ext` 关键词，反之亦然

## ArkWeb 的替代扩展机制

| 机制 | 说明 |
|------|------|
| C++ 虚函数继承 | `ArkWebBrowserHostExt` 继承 `CefBrowserHost`，`ArkWebClientExt` 继承 `CefClient` |
| `_for_include.cc` 注入 | 通过 `#include` 将扩展代码注入上游类的编译单元，扩展行为而不修改上游源文件 |
| CEF 公共 API 类型 | 直接使用 `CefString`、`CefRefPtr<>`、`cef_string_map_t` 等 C++ 层类型 |

## 桥接层在当前构建中的角色

- 桥接层仍被编译为 `libcef_dll_wrapper` 静态库（`BUILD.gn:1605`）
- 但仅被上游测试/示例程序依赖：`cefclient`、`cefsimple`、`ceftests`
- 在 ArkWeb 实际运行路径中**不起作用**

## 新增接口时的决策指引

| 新增内容 | 是否需要桥接层 | 说明 |
|----------|---------------|------|
| C++ 虚类接口（继承 `CefXxx`） | 不需要 | 纯 C++ 多态，无 ABI 边界 |
| 带 `CEF_CALLBACK` 函数指针的 C 结构体 | 需要 | 必须配套 cpptoc/ctocpp 生成桥接 |
| `ohos_cef_ext/include/` 中新增 OH 扩展接口 | 不需要 | 全部走 C++ 继承路径 |
