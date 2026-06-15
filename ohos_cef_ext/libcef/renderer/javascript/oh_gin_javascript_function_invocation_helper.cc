// Copyright (c) 2022 Huawei Device Co., Ltd.
// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/renderer/javascript/oh_gin_javascript_function_invocation_helper.h"

#include <sys/mman.h>

#include <utility>

#include "arkweb/build/features/features.h"
#include "arkweb/chromium_ext/base/ohos/render_uid_define.h"
#include "base/values.h"
#include "base/memory/read_only_shared_memory_region.h"
#include "base/memory/platform_shared_memory_region.h"
#include "content/public/renderer/v8_value_converter.h"
#include "libcef/common/javascript/oh_gin_javascript_bridge_errors.h"
#include "libcef/common/javascript/oh_gin_javascript_bridge_value.h"
#include "libcef/renderer/javascript/oh_gin_javascript_bridge_object.h"
#include "libcef/renderer/javascript/oh_gin_javascript_bridge_value_converter.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/bounds_checking_function/include/securec.h"
#include "third_party/ohos_ndk/includes/ohos_adapter/ohos_adapter_helper.h"
#include "v8/include/v8-exception.h"

#if BUILDFLAG(ARKWEB_RENDER_REMOVE_BINDER)
#include <unistd.h>
#include "base/command_line.h"
#include "content/public/common/content_switches.h"
#include "third_party/blink/renderer/core/render_mojom/render_mojom_client.h"
#endif  // BUILDFLAG(ARKWEB_RENDER_REMOVE_BINDER)

namespace {

const char kMethodInvocationAsConstructorDisallowed[] =
    "Javascript bridge method can't be invoked as a constructor";
const char kMethodInvocationOnNonInjectedObjectDisallowed[] =
    "Javascript bridge method can't be invoked on a non-injected object";
const char kMethodInvocationErrorMessage[] =
    "Javascript bridge method invocation error";

}  // namespace

namespace NWEB {
int32_t OhGinJavascriptFunctionInvocationHelper::maxFdNum_ = -1;
std::atomic<int32_t> OhGinJavascriptFunctionInvocationHelper::usedFd_{0};
const int MAX_FLOWBUF_DATA_SIZE = 52428800; /* 50 MB */
const int MAX_ENTRIES = 10;
const int HEADER_SIZE =
    (MAX_ENTRIES * 8); /* 10 * (int position + int length) */
const int INDEX_SIZE = 2;
const int DEFAULT_ID = 1073741824;

OhGinJavascriptFunctionInvocationHelper::
    OhGinJavascriptFunctionInvocationHelper(
        const std::string& method_name,
        const base::WeakPtr<OhGinJavascriptBridgeDispatcher>& dispatcher)
    : method_name_(method_name),
      dispatcher_(dispatcher),
      converter_(new OhGinJavascriptBridgeValueConverter(dispatcher)) {}

OhGinJavascriptFunctionInvocationHelper::
    ~OhGinJavascriptFunctionInvocationHelper() {}

bool OhGinJavascriptFunctionInvocationHelper::StoreString(int index,
                                                          void* mem,
                                                          const char* str,
                                                          size_t str_len) {
  if (str_len == 0 || str_len > static_cast<size_t>(MAX_FLOWBUF_DATA_SIZE)) {
    return false;
  }

  int i;
  int dataPos = 0;
  for (i = 0; i < MAX_ENTRIES; ++i) {
    int* entry = static_cast<int*>(mem) + (i * INDEX_SIZE);
    dataPos += *(entry + 1);
    if (*(entry + 1) == 0) {  // Check if length is 0, indicating unused memory
      break;
    }
  }
  // Check if the header is full
  if (i == MAX_ENTRIES) {
    LOG(DEBUG) << "Flowbuf header is full, cannot store more strings";
    return false;
  }

  char* dataMem = static_cast<char*>(mem) + HEADER_SIZE;
  // Check for available space in data port
  int len = static_cast<int>(str_len);
  if (dataPos + len > MAX_FLOWBUF_DATA_SIZE) {
    LOG(DEBUG) << "Flowbuf not enough space to store more strings";
    return false;
  }

  int* newEntry = static_cast<int*>(mem) + (i * INDEX_SIZE);
  *(newEntry) = index;
  *(newEntry + 1) = len;
  if (memcpy_s(dataMem + dataPos, MAX_FLOWBUF_DATA_SIZE - dataPos, str,
               len) != EOK) {
    LOG(DEBUG) << "Flowbuf memcpy fail, cannot store more strings";
    return false;
  }
  return true;
}

v8::Local<v8::Value> OhGinJavascriptFunctionInvocationHelper::Invoke(
    gin::Arguments* args) {
  if (!dispatcher_) {
    args->isolate()->ThrowException(v8::Exception::Error(
        gin::StringToV8(args->isolate(), kMethodInvocationErrorMessage)));
    return v8::Undefined(args->isolate());
  }

  if (args->IsConstructCall()) {
    args->isolate()->ThrowException(v8::Exception::Error(gin::StringToV8(
        args->isolate(), kMethodInvocationAsConstructorDisallowed)));
    return v8::Undefined(args->isolate());
  }

  OhGinJavascriptBridgeObject* object = nullptr;
  if (!args->GetHolder(&object) || !object) {
    args->isolate()->ThrowException(v8::Exception::Error(gin::StringToV8(
        args->isolate(), kMethodInvocationOnNonInjectedObjectDisallowed)));
    return v8::Undefined(args->isolate());
  }

  if (maxFdNum_ == -1) {
    maxFdNum_ = OHOS::NWeb::OhosAdapterHelper::GetInstance()
                    .GetSystemPropertiesInstance()
                    .GetFlowBufMaxFd();
  }

  base::Value::List arguments;
  content::mojom::OhGinJavascriptBridgeError error =
      content::mojom::OhGinJavascriptBridgeError::kOhGinJavascriptBridgeNoError;
  std::unique_ptr<base::Value> result;
  if (usedFd_.load() < maxFdNum_) {
    result = InvokeJavascriptMethodFlowbuf(arguments, error, args, object);
  } else {
    result = InvokeJavascriptMethod(arguments, error, args, object);
  }

  if (!result.get()) {
    LOG(DEBUG)
        << "OhGinJavascriptFunctionInvocationHelper::Invoke result is null";
    args->isolate()->ThrowException(v8::Exception::Error(gin::StringToV8(
        args->isolate(), OhGinJavascriptBridgeErrorToString(error))));
    return v8::Undefined(args->isolate());
  }
  if (!result->is_blob()) {
    LOG(DEBUG)
        << "OhGinJavascriptFunctionInvocationHelper::Invoke result is not blob";
    return converter_->ToV8Value(result.get(),
                                 args->isolate()->GetCurrentContext());
  }

  std::unique_ptr<const OhGinJavascriptBridgeValue> gin_value =
      OhGinJavascriptBridgeValue::FromValue(result.get());
  if (gin_value->IsType(OhGinJavascriptBridgeValue::TYPE_OBJECT_ID)) {
    OhGinJavascriptBridgeObject* object_result = NULL;
    OhGinJavascriptBridgeDispatcher::ObjectID object_id;
    if (gin_value->GetAsObjectID(&object_id)) {
      object_result = dispatcher_->GetObject(object_id);
    }
    if (object_result) {
      LOG(DEBUG) << "OhGinJavascriptFunctionInvocationHelper::Invoke result is "
                    "blob, object_id = "
                 << (uint32_t)object_id;
      gin::Handle<OhGinJavascriptBridgeObject> controller =
          gin::CreateHandle(args->isolate(), object_result);
      if (controller.IsEmpty()) {
        return v8::Undefined(args->isolate());
      }
      return controller.ToV8();
    }
  } else if (gin_value->IsType(OhGinJavascriptBridgeValue::TYPE_NONFINITE)) {
    float float_value;
    gin_value->GetAsNonFinite(&float_value);
    return v8::Number::New(args->isolate(), float_value);
  }
  return v8::Undefined(args->isolate());
}

std::unique_ptr<base::Value>
OhGinJavascriptFunctionInvocationHelper::InvokeJavascriptMethod(
    base::Value::List& arguments,
    content::mojom::OhGinJavascriptBridgeError& error,
    gin::Arguments* args,
    OhGinJavascriptBridgeObject* object) {
  auto* remote = object->GetRemote();
  if (!remote) {
    error = content::mojom::OhGinJavascriptBridgeError::kOhGinJavascriptBridgeObjectIsGone;
    return nullptr;
  }
  {
    v8::HandleScope handle_scope(args->isolate());
    v8::Local<v8::Context> context = args->isolate()->GetCurrentContext();
    v8::Local<v8::Value> val;
    while (args->GetNext(&val)) {
      std::unique_ptr<base::Value> arg(converter_->FromV8Value(val, context));
      LOG(DEBUG) << "OhGinJavascriptFunctionInvocationHelper::Invoke call "
                    "FromV8Value end";
      if (arg.get()) {
        arguments.Append(base::Value::FromUniquePtrValue(std::move(arg)));
      } else {
        arguments.Append(
            base::Value::FromUniquePtrValue(std::make_unique<base::Value>()));
      }
    }
  }

  std::string url = "";
  blink::WebLocalFrame* frame = blink::WebLocalFrame::FrameForContext(
      args->isolate()->GetCurrentContext());
  if (frame) {
    url = frame->GetDocument().Url().GetString().Utf8();
  }
  std::unique_ptr<base::Value> result;
  if (dispatcher_->IsAsyncMethod(object->object_id(), method_name_)) {
    remote->InvokeMethodAsync(method_name_, url, std::move(arguments));
    LOG(DEBUG) << "OhGinJavascriptFunctionInvocationHelper::Invoke call "
                  "InvokeJavascriptMethodAsync end";
    return base::Value::ToUniquePtrValue(base::Value(0).Clone());
  } else {
    base::Value::List result_wrapper;
    remote->InvokeMethod(method_name_, url, std::move(arguments), &error,
                               &result_wrapper);
    LOG(DEBUG) << "OhGinJavascriptFunctionInvocationHelper::Invoke call "
                  "InvokeJavascriptMethod end";
    if (!result_wrapper.empty()) {
      result = base::Value::ToUniquePtrValue(result_wrapper[0].Clone());
    }
    return result;
  }
}

std::unique_ptr<base::Value>
OhGinJavascriptFunctionInvocationHelper::InvokeJavascriptMethodFlowbuf(
    base::Value::List& arguments,
    content::mojom::OhGinJavascriptBridgeError& error,
    gin::Arguments* args,
    OhGinJavascriptBridgeObject* object) {
  if (dispatcher_->IsAsyncMethod(object->object_id(), method_name_)) {
    return InvokeJavascriptMethod(arguments, error, args, object);
  }

  auto flowbufferAdapter =
      OHOS::NWeb::OhosAdapterHelper::GetInstance().CreateFlowbufferAdapter();
  if (!flowbufferAdapter || (object->object_id() >= DEFAULT_ID)) {
    return InvokeJavascriptMethod(arguments, error, args, object);
  }

#if BUILDFLAG(ARKWEB_RENDER_REMOVE_BINDER)
  uid_t uid = getuid();
  if (OHOS::NWeb::IsRenderProcessByUid(uid)) {
    blink::ResSchedReportClient report_client;
    report_client.StartPerformanceBoost();
  } else {
    flowbufferAdapter->StartPerformanceBoost();
  }
#else
  flowbufferAdapter->StartPerformanceBoost();
#endif  // BUILDFLAG(ARKWEB_RENDER_REMOVE_BINDER)

  constexpr size_t region_size = MAX_FLOWBUF_DATA_SIZE + HEADER_SIZE;
  base::WritableSharedMemoryRegion writable_region =
      base::WritableSharedMemoryRegion::Create(region_size);
  if (!writable_region.IsValid()) {
    LOG(ERROR) << "Failed to create WritableSharedMemoryRegion";
    return InvokeJavascriptMethod(arguments, error, args, object);
  }

  base::WritableSharedMemoryMapping mapping = writable_region.Map();
  if (!mapping.IsValid()) {
    LOG(ERROR) << "Failed to map WritableSharedMemoryRegion";
    return InvokeJavascriptMethod(arguments, error, args, object);
  }

  int index = -1;
  bool useFlowbuf = false;
  {
    v8::HandleScope handle_scope(args->isolate());
    v8::Local<v8::Context> context = args->isolate()->GetCurrentContext();
    v8::Local<v8::Value> val;
    while (args->GetNext(&val)) {
      index++;
      if (val->IsString()) {
        v8::String::Utf8Value str(args->isolate(), val);
        size_t len = static_cast<size_t>(str.length()) + 1;
        if (StoreString(index, mapping.memory(), *str, len)) {
          useFlowbuf = true;
          continue;
        }
      }

      std::unique_ptr<base::Value> arg(converter_->FromV8Value(val, context));
      LOG(DEBUG) << "OhGinJavascriptFunctionInvocationHelper::Invoke call "
                    "FromV8Value end";
      if (arg.get()) {
        arguments.Append(base::Value::FromUniquePtrValue(std::move(arg)));
      } else {
        arguments.Append(
            base::Value::FromUniquePtrValue(std::make_unique<base::Value>()));
      }
    }
  }

  std::string url = "";
  blink::WebLocalFrame* frame = blink::WebLocalFrame::FrameForContext(
      args->isolate()->GetCurrentContext());
  if (frame) {
    url = frame->GetDocument().Url().GetString().Utf8();
  }
  std::unique_ptr<base::Value> result;
  auto* remote = object->GetRemote();
  if (!remote) {
    error = content::mojom::OhGinJavascriptBridgeError::kOhGinJavascriptBridgeObjectIsGone;
    return nullptr;
  }
  base::Value::List result_wrapper;
  if (useFlowbuf) {
    usedFd_.fetch_add(1);
    result = InvokeMethodFlowbufMojo(object,
                                     url,
                                     method_name_,
                                     arguments,
                                     std::move(writable_region),
                                     &error);
    usedFd_.fetch_sub(1);
    LOG(INFO) << "OhGinJavascriptFunctionInvocationHelper::Invoke call "
                  "InvokeMethodFlowbufMojo end";
    return result;
  } else {
    remote->InvokeMethod(method_name_, url, std::move(arguments), &error,
                               &result_wrapper);
    LOG(DEBUG) << "OhGinJavascriptFunctionInvocationHelper::Invoke call "
                  "InvokeJavascriptMethod end";
  }
  if (!result_wrapper.empty()) {
    result = base::Value::ToUniquePtrValue(result_wrapper[0].Clone());
  }
  return result;
}

std::unique_ptr<base::Value>
OhGinJavascriptFunctionInvocationHelper::InvokeMethodFlowbufMojo(
    OhGinJavascriptBridgeObject* object,
    const std::string& url,
    const std::string& method_name,
    base::Value::List& arguments,
    base::WritableSharedMemoryRegion writable_region,
    content::mojom::OhGinJavascriptBridgeError* error) {

  base::ReadOnlySharedMemoryRegion region =
    base::WritableSharedMemoryRegion::ConvertToReadOnly(std::move(writable_region));

  if (!region.IsValid()) {
    LOG(ERROR) << "OhGinJavascriptFunctionInvocationHelper::InvokeMethodFlowbufMojo "
               << "region Deserialize failed";
    *error = content::mojom::OhGinJavascriptBridgeError::kOhGinJavascriptBridgeClientDeleted;
    return nullptr;
  }

  auto* remote = object->GetRemote();
  if (!remote) {
    *error = content::mojom::OhGinJavascriptBridgeError::kOhGinJavascriptBridgeObjectIsGone;
    return nullptr;
  }

  content::mojom::OhGinJavascriptBridgeError mojo_error_code;
  base::Value::List mojo_result;
  bool success = remote->InvokeMethodFlowbuf(method_name,
                                             url,
                                             std::move(arguments),
                                             std::move(region),
                                             &mojo_error_code,
                                             &mojo_result);
  if (!success) {
    *error = content::mojom::OhGinJavascriptBridgeError::kOhGinJavascriptBridgeClientDeleted;
    return nullptr;
  }

  *error = mojo_error_code;
  if (mojo_error_code != content::mojom::OhGinJavascriptBridgeError::kOhGinJavascriptBridgeNoError) {
    return nullptr;
  }
  if (!mojo_result.empty()) {
    return std::make_unique<base::Value>(std::move(mojo_result[0]));
  }
  return nullptr;
}

}  // namespace NWEB
