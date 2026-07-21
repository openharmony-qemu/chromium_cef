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

class ActionOpenPopupFunction : public ExtensionFunction {
 public:
  DECLARE_EXTENSION_FUNCTION("action.openPopup", ACTION_OPENPOPUP)

  ActionOpenPopupFunction();
  ActionOpenPopupFunction(const ActionOpenPopupFunction&) = delete;
  ActionOpenPopupFunction& operator=(const ActionOpenPopupFunction&) = delete;

 protected:
  ~ActionOpenPopupFunction() override;
  ResponseAction Run() override;

  void OnShowPopupComplete(ExtensionHost* popup_host);

 private:
  ResponseAction RunOpenPopupFunctionForInclude();

  static void OpenPopupCallback(
      const base::WeakPtr<ActionOpenPopupFunction>& function,
      const std::optional<std::string>& error);

  bool call_action_open_popup_ = false;
  base::WeakPtrFactory<ActionOpenPopupFunction> weak_ptr_factory_{this};
};
