// Copyright (c) 2025 Huawei Device Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cef/ohos_cef_ext/libcef/browser/color_chooser/color_chooser_ohos.h"

#include "cef/ohos_cef_ext/include/arkweb_dialog_handler_ext.h"

namespace {

class ColorChooserCallbackImpl : public CefColorChooserCallback {
 public:
  explicit ColorChooserCallbackImpl(ColorChooserOhos* chooser)
      : chooser_(chooser) {}

  void Continue(uint32_t color) override {
    if (chooser_) {
      chooser_->OnColorChosen(color);
    }
  }

  void Cancel() override {
    if (chooser_) {
      chooser_->End();
    }
  }

 private:
  raw_ptr<ColorChooserOhos> chooser_;

  IMPLEMENT_REFCOUNTING(ColorChooserCallbackImpl);
};

}  // namespace

ColorChooserOhos::ColorChooserOhos(
    CefRefPtr<CefClient> client,
    content::WebContents* web_contents,
    SkColor initial_color,
    const std::vector<blink::mojom::ColorSuggestionPtr>& suggestions)
    : client_(client), web_contents_(web_contents) {
  if (auto handler = client_->GetDialogHandler()) {
    CefRefPtr<CefColorChooserCallback> callback =
        new ColorChooserCallbackImpl(this);
    handler->OnColorChooserShow(static_cast<uint32_t>(initial_color), callback);
  }
}

ColorChooserOhos::~ColorChooserOhos() = default;

void ColorChooserOhos::End() {
  web_contents_->DidEndColorChooser();
}

void ColorChooserOhos::SetSelectedColor(SkColor color) {}

void ColorChooserOhos::OnColorChosen(uint32_t color) {
  web_contents_->DidChooseColorInColorChooser(color);
  web_contents_->DidEndColorChooser();
}
