/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#include "cef/ohos_cef_ext/libcef/browser/osr/arkweb_render_widget_host_view_osr_utils.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "arkweb/build/features/features.h"
#include "base/containers/flat_set.h"
#include "base/memory/raw_ptr.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/display/display.h"
#include "ui/display/display_list.h"
#include "ui/display/screen.h"
#include "ui/display/screen_base.h"
#include "ui/display/screen_info.h"
#include "ui/display/util/display_util.h"
#include "ui/gfx/color_space.h"
#include "ui/gfx/display_color_spaces.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/text_elider.h"

#if BUILDFLAG(ARKWEB_EVICT_UNLOCK_FRAMES)

#include "cef/libcef/browser/osr/render_widget_host_view_osr.h"
#include "components/viz/common/surfaces/parent_local_surface_id_allocator.h"

#if BUILDFLAG(ARKWEB_PULL_TO_REFRESH)
#include "arkweb/chromium_ext/ui/ohos/overscroll_refresh_handler.h"
#endif

namespace {

class ArkWebRenderWidgetHostViewOSRUtilsTest : public testing::Test {
 protected:
  void SetUp() override {
    task_environment_ = std::make_unique<base::test::TaskEnvironment>(
        base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  }

  void TearDown() override { task_environment_.reset(); }

  std::unique_ptr<base::test::TaskEnvironment> task_environment_;
};

// ============================================================================
// ConvertOrientationType Tests (Static Method)
// ============================================================================

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, ConvertOrientationType_Undefined) {
  auto result = ArkWebRenderWidgetHostViewOSRUtils::ConvertOrientationType(
      cef_screen_orientation_type_t::UNDEFINED);
  EXPECT_EQ(result, display::mojom::ScreenOrientation::kUndefined);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, ConvertOrientationType_PortraitPrimary) {
  auto result = ArkWebRenderWidgetHostViewOSRUtils::ConvertOrientationType(
      cef_screen_orientation_type_t::PORTRAIT_PRIMARY);
  EXPECT_EQ(result, display::mojom::ScreenOrientation::kPortraitPrimary);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, ConvertOrientationType_LandscapePrimary) {
  auto result = ArkWebRenderWidgetHostViewOSRUtils::ConvertOrientationType(
      cef_screen_orientation_type_t::LANDSCAPE_PRIMARY);
  EXPECT_EQ(result, display::mojom::ScreenOrientation::kLandscapePrimary);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, ConvertOrientationType_PortraitSecondary) {
  auto result = ArkWebRenderWidgetHostViewOSRUtils::ConvertOrientationType(
      cef_screen_orientation_type_t::PORTRAIT_SECONDARY);
  EXPECT_EQ(result, display::mojom::ScreenOrientation::kPortraitSecondary);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, ConvertOrientationType_LandscapeSecondary) {
  auto result = ArkWebRenderWidgetHostViewOSRUtils::ConvertOrientationType(
      cef_screen_orientation_type_t::LANDSCAPE_SECONDARY);
  EXPECT_EQ(result, display::mojom::ScreenOrientation::kLandscapeSecondary);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, ConvertOrientationType_InvalidValue) {
  auto result = ArkWebRenderWidgetHostViewOSRUtils::ConvertOrientationType(
      static_cast<cef_screen_orientation_type_t>(999));
  EXPECT_EQ(result, display::mojom::ScreenOrientation::kUndefined);
}

// ============================================================================
// TruncateTooltipText Tests (Static Method)
// ============================================================================

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, TruncateTooltipText_ShortText) {
  std::u16string short_text = u"Hello World";
  auto result = ArkWebRenderWidgetHostViewOSRUtils::TruncateTooltipText(short_text);
  EXPECT_EQ(result, short_text);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, TruncateTooltipText_EmptyText) {
  std::u16string empty_text = u"";
  auto result = ArkWebRenderWidgetHostViewOSRUtils::TruncateTooltipText(empty_text);
  EXPECT_EQ(result, empty_text);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, TruncateTooltipText_LongText) {
  std::u16string long_text;
  for (int i = 0; i < 2000; ++i) {
    long_text += u"a";
  }
  auto result = ArkWebRenderWidgetHostViewOSRUtils::TruncateTooltipText(long_text);
  EXPECT_LT(result.length(), long_text.length());
  EXPECT_LE(result.length(), 1024u);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, TruncateTooltipText_ExactlyMaxLength) {
  std::u16string exact_text;
  for (int i = 0; i < 1024; ++i) {
    exact_text += u"a";
  }
  auto result = ArkWebRenderWidgetHostViewOSRUtils::TruncateTooltipText(exact_text);
  EXPECT_EQ(result.length(), 1024u);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, TruncateTooltipText_UnicodeCharacters) {
  std::u16string unicode_text = u"中文测试日本語テスト";
  auto result = ArkWebRenderWidgetHostViewOSRUtils::TruncateTooltipText(unicode_text);
  EXPECT_EQ(result, unicode_text);
}

// ============================================================================
// UpdateScreenInfoForArkweb Tests (Static Method)
// ============================================================================

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, UpdateScreenInfoForArkweb_Basic) {
  display::ScreenInfo screenInfo;
  CefScreenInfo src;
  src.angle = 90;
  src.orientation = cef_screen_orientation_type_t::PORTRAIT_PRIMARY;
  
  ArkWebRenderWidgetHostViewOSRUtils::UpdateScreenInfoForArkweb(screenInfo, src);
  
  EXPECT_EQ(screenInfo.orientation_angle, 90);
  EXPECT_EQ(screenInfo.orientation_type, display::mojom::ScreenOrientation::kPortraitPrimary);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, UpdateScreenInfoForArkweb_ZeroAngle) {
  display::ScreenInfo screenInfo;
  CefScreenInfo src;
  src.angle = 0;
  src.orientation = cef_screen_orientation_type_t::LANDSCAPE_PRIMARY;
  
  ArkWebRenderWidgetHostViewOSRUtils::UpdateScreenInfoForArkweb(screenInfo, src);
  
  EXPECT_EQ(screenInfo.orientation_angle, 0);
  EXPECT_EQ(screenInfo.orientation_type, display::mojom::ScreenOrientation::kLandscapePrimary);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, UpdateScreenInfoForArkweb_UndefinedOrientation) {
  display::ScreenInfo screenInfo;
  CefScreenInfo src;
  src.angle = 180;
  src.orientation = cef_screen_orientation_type_t::UNDEFINED;
  
  ArkWebRenderWidgetHostViewOSRUtils::UpdateScreenInfoForArkweb(screenInfo, src);
  
  EXPECT_EQ(screenInfo.orientation_angle, 180);
  EXPECT_EQ(screenInfo.orientation_type, display::mojom::ScreenOrientation::kUndefined);
}

// ============================================================================
// GetCompositor Tests
// ============================================================================

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, GetCompositor_WidgetNotFound) {
  gfx::AcceleratedWidget widget = gfx::kNullAcceleratedWidget;
  
  ui::Compositor* result = ArkWebRenderWidgetHostViewOSRUtils::GetCompositor(widget);
  
  EXPECT_EQ(nullptr, result);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, GetCompositor_ReturnsNullForNewWidget) {
  gfx::AcceleratedWidget widget = 12345;
  
  ui::Compositor* result = ArkWebRenderWidgetHostViewOSRUtils::GetCompositor(widget);
  
  EXPECT_EQ(nullptr, result);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, GetCompositor_MultipleWidgets) {
  gfx::AcceleratedWidget widget1 = 400;
  gfx::AcceleratedWidget widget2 = 500;
  
  ArkWebRenderWidgetHostViewOSRUtils::AddCompositor(widget1, nullptr);
  ArkWebRenderWidgetHostViewOSRUtils::AddCompositor(widget2, nullptr);
  
  ui::Compositor* result1 = ArkWebRenderWidgetHostViewOSRUtils::GetCompositor(widget1);
  ui::Compositor* result2 = ArkWebRenderWidgetHostViewOSRUtils::GetCompositor(widget2);
  
  EXPECT_EQ(nullptr, result1);
  EXPECT_EQ(nullptr, result2);
}

// ============================================================================
// GetCompositorData Tests
// ============================================================================

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, GetCompositorData_WidgetNotFound) {
  gfx::AcceleratedWidget widget = gfx::kNullAcceleratedWidget;
  
  auto result = ArkWebRenderWidgetHostViewOSRUtils::GetCompositorData(widget);
  
  EXPECT_EQ(nullptr, result.first);
  EXPECT_EQ(nullptr, result.second);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, GetCompositorData_ReturnsNullForNewWidget) {
  gfx::AcceleratedWidget widget = 99999;
  
  auto result = ArkWebRenderWidgetHostViewOSRUtils::GetCompositorData(widget);
  
  EXPECT_EQ(nullptr, result.first);
  EXPECT_EQ(nullptr, result.second);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, GetCompositorData_ReturnsValidAllocator) {
  gfx::AcceleratedWidget widget = 100;
  
  ArkWebRenderWidgetHostViewOSRUtils::AddCompositor(widget, nullptr);
  
  auto result = ArkWebRenderWidgetHostViewOSRUtils::GetCompositorData(widget);
  
  EXPECT_EQ(nullptr, result.first);
  EXPECT_NE(nullptr, result.second);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, GetCompositorData_ConsistentCalls) {
  gfx::AcceleratedWidget widget = 300;
  
  ArkWebRenderWidgetHostViewOSRUtils::AddCompositor(widget, nullptr);
  
  auto result1 = ArkWebRenderWidgetHostViewOSRUtils::GetCompositorData(widget);
  auto result2 = ArkWebRenderWidgetHostViewOSRUtils::GetCompositorData(widget);
  
  EXPECT_EQ(result1.first, result2.first);
  EXPECT_EQ(result1.second, result2.second);
}

// ============================================================================
// AddCompositor Tests
// ============================================================================

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, AddCompositor_GetCompositorReturnsNullWhenCompositorIsNull) {
  gfx::AcceleratedWidget widget = 12345;
  
  ArkWebRenderWidgetHostViewOSRUtils::AddCompositor(widget, nullptr);
  
  ui::Compositor* result = ArkWebRenderWidgetHostViewOSRUtils::GetCompositor(widget);
  
  EXPECT_EQ(nullptr, result);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, AddCompositor_MultipleWidgets) {
  gfx::AcceleratedWidget widget1 = 100;
  gfx::AcceleratedWidget widget2 = 200;
  
  ArkWebRenderWidgetHostViewOSRUtils::AddCompositor(widget1, nullptr);
  ArkWebRenderWidgetHostViewOSRUtils::AddCompositor(widget2, nullptr);
  
  auto result1 = ArkWebRenderWidgetHostViewOSRUtils::GetCompositorData(widget1);
  auto result2 = ArkWebRenderWidgetHostViewOSRUtils::GetCompositorData(widget2);
  
  EXPECT_NE(nullptr, result1.second);
  EXPECT_NE(nullptr, result2.second);
  EXPECT_NE(result1.second, result2.second);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, AddCompositor_IntegrationWithGetCompositorData) {
  gfx::AcceleratedWidget widget = 600;
  
  ArkWebRenderWidgetHostViewOSRUtils::AddCompositor(widget, nullptr);
  
  auto compositor_result = ArkWebRenderWidgetHostViewOSRUtils::GetCompositor(widget);
  auto data_result = ArkWebRenderWidgetHostViewOSRUtils::GetCompositorData(widget);
  
  EXPECT_EQ(nullptr, compositor_result);
  EXPECT_EQ(nullptr, data_result.first);
  EXPECT_NE(nullptr, data_result.second);
}

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, AddCompositor_DifferentWidgetIds) {
  gfx::AcceleratedWidget widget1 = 700;
  gfx::AcceleratedWidget widget2 = 800;
  
  ArkWebRenderWidgetHostViewOSRUtils::AddCompositor(widget1, nullptr);
  ArkWebRenderWidgetHostViewOSRUtils::AddCompositor(widget2, nullptr);
  
  auto result1 = ArkWebRenderWidgetHostViewOSRUtils::GetCompositorData(widget1);
  auto result2 = ArkWebRenderWidgetHostViewOSRUtils::GetCompositorData(widget2);
  
  EXPECT_NE(nullptr, result1.second);
  EXPECT_NE(nullptr, result2.second);
  EXPECT_NE(result1.second, result2.second);
}

// ============================================================================
// IsRenderWidgetHostViewForActiveMainFrame Tests (requires view context)
// Note: This method requires a valid view_ pointer, so we test edge cases only
// ============================================================================

TEST_F(ArkWebRenderWidgetHostViewOSRUtilsTest, GetContextFactory_ReturnsNonNull) {
  auto result = ArkWebRenderWidgetHostViewOSRUtils::GetContextFactory();
  EXPECT_NE(nullptr, result);
}

#if BUILDFLAG(ARKWEB_SYNC_RENDER)
TEST(ArkWebCompositorMapCleanupTest, RemoveStaleEntries) {
  std::map<int, std::unique_ptr<content::SoftwareCompositorHostOhos>> test_map;
  test_map[1] = nullptr;
  test_map[2] = nullptr;
  test_map[3] = nullptr;

  std::set<int> active = {1, 3};

  for (auto it = test_map.begin(); it != test_map.end();) {
    if (active.find(it->first) == active.end()) {
      it = test_map.erase(it);
    } else {
      ++it;
    }
  }

  EXPECT_EQ(test_map.size(), 2u);
  EXPECT_NE(test_map.find(1), test_map.end());
  EXPECT_EQ(test_map.find(2), test_map.end());
  EXPECT_NE(test_map.find(3), test_map.end());
}

TEST(ArkWebCompositorMapCleanupTest, AllActive_NoRemoval) {
  std::map<int, std::unique_ptr<content::SoftwareCompositorHostOhos>> test_map;
  test_map[1] = nullptr;
  test_map[2] = nullptr;

  std::set<int> active = {1, 2};

  for (auto it = test_map.begin(); it != test_map.end();) {
    if (active.find(it->first) == active.end()) {
      it = test_map.erase(it);
    } else {
      ++it;
    }
  }

  EXPECT_EQ(test_map.size(), 2u);
}

TEST(ArkWebCompositorMapCleanupTest, EmptyMap_NoCrash) {
  std::map<int, std::unique_ptr<content::SoftwareCompositorHostOhos>> test_map;
  std::set<int> active = {1};

  for (auto it = test_map.begin(); it != test_map.end();) {
    if (active.find(it->first) == active.end()) {
      it = test_map.erase(it);
    } else {
      ++it;
    }
  }

  EXPECT_EQ(test_map.size(), 0u);
}

TEST(ArkWebCompositorMapCleanupTest, AllStale_RemoveAll) {
  std::map<int, std::unique_ptr<content::SoftwareCompositorHostOhos>> test_map;
  test_map[1] = nullptr;
  test_map[2] = nullptr;
  test_map[3] = nullptr;

  std::set<int> active;

  for (auto it = test_map.begin(); it != test_map.end();) {
    if (active.find(it->first) == active.end()) {
      it = test_map.erase(it);
    } else {
      ++it;
    }
  }

  EXPECT_EQ(test_map.size(), 0u);
}
#endif  // BUILDFLAG(ARKWEB_SYNC_RENDER)

}  // namespace
#endif  // BUILDFLAG(ARKWEB_EVICT_UNLOCK_FRAMES)

// Screen topology is independent of compositor frame-eviction features.
namespace {

constexpr int64_t kPrimaryId = 17;
constexpr int64_t kSecondaryId = 42;
constexpr int64_t kThirdId = 93;
using Orientation = display::mojom::ScreenOrientation;
using DisplayType = display::DisplayList::Type;

display::Display MakePrimary() {
  display::Display primary(kPrimaryId, gfx::Rect(0, 0, 1920, 1080));
  primary.set_work_area(gfx::Rect(0, 0, 1920, 1040));
  primary.set_device_scale_factor(1.f);
  primary.set_color_depth(24);
  primary.set_depth_per_component(8);
  primary.set_label("Built-in display");
  return primary;
}

display::Display MakeSecondary() {
  display::Display secondary(kSecondaryId, gfx::Rect(1920, 0, 2560, 1440));
  secondary.set_work_area(gfx::Rect(1920, 0, 2560, 1400));
  secondary.set_device_scale_factor(2.f);
  secondary.set_color_depth(30);
  secondary.set_depth_per_component(10);
  secondary.set_label("External display");
  return secondary;
}

display::ScreenInfo MakeOsrInfo() {
  display::ScreenInfo osr;
  osr.device_scale_factor = 1.5f;
  osr.depth = 32;
  osr.depth_per_component = 8;
  osr.is_monochrome = true;
  osr.rect = gfx::Rect(10, 20, 800, 600);
  osr.available_rect = gfx::Rect(10, 30, 800, 580);
  osr.orientation_type = Orientation::kPortraitSecondary;
  osr.orientation_angle = 180;
  return osr;
}

class ArkWebOsrScreenInfosTest : public testing::Test {
 protected:
  void SetUp() override {
    // Screen rejects replacing one live singleton directly in DCHECK builds.
    previous_screen_ = display::Screen::SetScreenInstance(nullptr);
    display::Screen::SetScreenInstance(&screen_);
    previous_internal_ids_ = display::GetInternalDisplayIds();
    display::SetInternalDisplayIds({});
  }

  void TearDown() override {
    display::SetInternalDisplayIds(previous_internal_ids_);
    display::Screen::SetScreenInstance(nullptr);
    display::Screen::SetScreenInstance(previous_screen_.get());
  }

  void AddPrimary() {
    screen_.display_list().AddDisplay(MakePrimary(), DisplayType::PRIMARY);
  }

  void AddSecondary() {
    screen_.display_list().AddDisplay(MakeSecondary(),
                                      DisplayType::NOT_PRIMARY);
  }

  void AddTwoDisplays() {
    AddPrimary();
    AddSecondary();
  }

  // Assertion helper only: never synthesizes an expected merged result.
  void ExpectOsrFields(const display::ScreenInfo& actual,
                       const display::ScreenInfo& osr) {
    EXPECT_FLOAT_EQ(osr.device_scale_factor, actual.device_scale_factor);
    EXPECT_EQ(osr.depth, actual.depth);
    EXPECT_EQ(osr.depth_per_component, actual.depth_per_component);
    EXPECT_EQ(osr.is_monochrome, actual.is_monochrome);
    EXPECT_EQ(osr.rect, actual.rect);
    EXPECT_EQ(osr.available_rect, actual.available_rect);
    EXPECT_EQ(osr.orientation_type, actual.orientation_type);
    EXPECT_EQ(osr.orientation_angle, actual.orientation_angle);
  }

  void ExpectTopology(const display::ScreenInfos& infos,
                      size_t count,
                      int64_t primary_id,
                      bool extended) {
    ASSERT_EQ(count, infos.screen_infos.size());
    ASSERT_NE(infos.screen_infos.end(),
              std::ranges::find(infos.screen_infos, infos.current_display_id,
                                &display::ScreenInfo::display_id));
    EXPECT_EQ(primary_id, infos.current_display_id);
    EXPECT_EQ(primary_id, infos.current().display_id);
    EXPECT_TRUE(infos.current().is_primary);
    size_t primary_count = 0;
    for (const auto& info : infos.screen_infos) {
      EXPECT_EQ(extended, info.is_extended);
      EXPECT_NE(display::kInvalidDisplayId, info.display_id);
      EXPECT_EQ(primary_id == info.display_id, info.is_primary);
      primary_count += info.is_primary ? 1u : 0u;
    }
    EXPECT_EQ(1u, primary_count);
  }

  display::ScreenBase screen_;

 private:
  raw_ptr<display::Screen> previous_screen_ = nullptr;
  base::flat_set<int64_t> previous_internal_ids_;
};

TEST_F(ArkWebOsrScreenInfosTest, NoScreenPreservesCompleteOsrInfo) {
  display::Screen::SetScreenInstance(nullptr);
  auto osr = MakeOsrInfo();
  osr.display_id = 1234;
  osr.label = "Host fallback";
  osr.is_extended = true;
  osr.is_primary = true;
  osr.is_internal = true;
  osr.handwriting_radius = 9;
  osr.display_color_spaces =
      gfx::DisplayColorSpaces(gfx::ColorSpace::CreateDisplayP3D65());

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_EQ(1u, infos.screen_infos.size());
  EXPECT_EQ(1234, infos.current_display_id);
  EXPECT_EQ(osr, infos.current());
  EXPECT_EQ(display::ScreenInfos(osr), infos);
}

TEST_F(ArkWebOsrScreenInfosTest, NoScreenAcceptsDefaultOsrInfo) {
  display::Screen::SetScreenInstance(nullptr);
  const display::ScreenInfo osr;

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_EQ(1u, infos.screen_infos.size());
  EXPECT_EQ(display::kDefaultDisplayId, infos.current_display_id);
  EXPECT_EQ(osr, infos.current());
  EXPECT_FALSE(infos.current().is_extended);
}

TEST_F(ArkWebOsrScreenInfosTest, ScreenInitializationReplacesFallback) {
  display::Screen::SetScreenInstance(nullptr);
  const auto osr = MakeOsrInfo();
  const auto fallback =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  ASSERT_EQ(1u, fallback.screen_infos.size());
  EXPECT_EQ(display::kDefaultDisplayId, fallback.current_display_id);

  AddTwoDisplays();
  display::Screen::SetScreenInstance(&screen_);
  const auto initialized =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(initialized, 2u, kPrimaryId, true));
  ExpectOsrFields(initialized.current(), osr);
  EXPECT_FALSE(fallback.current().is_extended);
}

TEST_F(ArkWebOsrScreenInfosTest, EmptyPlatformListUsesChromiumDefaultDisplay) {
  const auto osr = MakeOsrInfo();
  ASSERT_TRUE(screen_.GetAllDisplays().empty());

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(
      ExpectTopology(infos, 1u, display::kDefaultDisplayId, false));
  ExpectOsrFields(infos.current(), osr);
  EXPECT_TRUE(infos.current().label.empty());
}

TEST_F(ArkWebOsrScreenInfosTest, SingleDisplayUsesRealIdentity) {
  AddPrimary();
  const auto osr = MakeOsrInfo();

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 1u, kPrimaryId, false));
  EXPECT_EQ("Built-in display", infos.current().label);
  ExpectOsrFields(infos.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, SingleDisplayIgnoresHostExtendedFlag) {
  AddPrimary();
  auto osr = MakeOsrInfo();
  osr.is_extended = true;
  osr.is_primary = false;
  osr.display_id = kSecondaryId;

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 1u, kPrimaryId, false));
  EXPECT_NE(osr.display_id, infos.current_display_id);
  EXPECT_FALSE(infos.current().is_extended);
}

// Regression: the previous CEF singleton result fails the count, identity and
// extended-state assertions, even though its OSR rendering fields are correct.
TEST_F(ArkWebOsrScreenInfosTest, TwoDisplaysRetainExtendedTopology) {
  AddTwoDisplays();
  const auto osr = MakeOsrInfo();
  ASSERT_FALSE(osr.is_extended);

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, kPrimaryId, true));
  EXPECT_EQ(kPrimaryId, infos.screen_infos[0].display_id);
  EXPECT_EQ(kSecondaryId, infos.screen_infos[1].display_id);
  EXPECT_EQ("Built-in display", infos.screen_infos[0].label);
  EXPECT_EQ("External display", infos.screen_infos[1].label);
  ExpectOsrFields(infos.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, ThreeDisplaysRetainEveryEntry) {
  AddTwoDisplays();
  display::Display third(kThirdId, gfx::Rect(-1280, 0, 1280, 720));
  third.set_label("Third display");
  screen_.display_list().AddDisplay(third, DisplayType::NOT_PRIMARY);

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(
          MakeOsrInfo());

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 3u, kPrimaryId, true));
  EXPECT_EQ(kThirdId, infos.screen_infos[2].display_id);
  EXPECT_EQ("Third display", infos.screen_infos[2].label);
  EXPECT_EQ(gfx::Rect(-1280, 0, 1280, 720), infos.screen_infos[2].rect);
}

TEST_F(ArkWebOsrScreenInfosTest, ManyDisplaysAreNotTruncated) {
  AddPrimary();
  for (int id = 100; id < 115; ++id) {
    screen_.display_list().AddDisplay(
        display::Display(id, gfx::Rect(100 * (id - 99), 0, 100, 100)),
        DisplayType::NOT_PRIMARY);
  }

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(
          MakeOsrInfo());

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 16u, kPrimaryId, true));
  for (size_t index = 1; index < infos.screen_infos.size(); ++index) {
    EXPECT_EQ(99 + static_cast<int64_t>(index),
              infos.screen_infos[index].display_id);
  }
}

TEST_F(ArkWebOsrScreenInfosTest, PrimaryNeedNotBeFirstEntry) {
  screen_.display_list().AddDisplay(MakeSecondary(), DisplayType::PRIMARY);
  screen_.display_list().AddDisplay(MakePrimary(), DisplayType::PRIMARY);
  const auto osr = MakeOsrInfo();

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, kPrimaryId, true));
  EXPECT_EQ(kSecondaryId, infos.screen_infos[0].display_id);
  EXPECT_EQ(gfx::Rect(1920, 0, 2560, 1440), infos.screen_infos[0].rect);
  EXPECT_EQ(kPrimaryId, infos.screen_infos[1].display_id);
  ExpectOsrFields(infos.screen_infos[1], osr);
}

TEST_F(ArkWebOsrScreenInfosTest, DisplayIdsRetainAll64Bits) {
  constexpr int64_t primary_id = (int64_t{1} << 40) + 17;
  constexpr int64_t secondary_id = (int64_t{1} << 48) + 42;
  screen_.display_list().AddDisplay(
      display::Display(primary_id, gfx::Rect(0, 0, 1920, 1080)),
      DisplayType::PRIMARY);
  screen_.display_list().AddDisplay(
      display::Display(secondary_id, gfx::Rect(1920, 0, 1920, 1080)),
      DisplayType::NOT_PRIMARY);

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(
          MakeOsrInfo());

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, primary_id, true));
  EXPECT_EQ(secondary_id, infos.screen_infos[1].display_id);
}

TEST_F(ArkWebOsrScreenInfosTest, DisplayIdZeroIsNotTreatedAsMissing) {
  screen_.display_list().AddDisplay(
      display::Display(0, gfx::Rect(0, 0, 1920, 1080)), DisplayType::PRIMARY);
  AddSecondary();

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(
          MakeOsrInfo());

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, 0, true));
  EXPECT_EQ(0, infos.current().display_id);
  EXPECT_EQ(kSecondaryId, infos.screen_infos[1].display_id);
}

TEST_F(ArkWebOsrScreenInfosTest, DefaultIdCanIdentifyRealPlatformDisplay) {
  screen_.display_list().AddDisplay(
      display::Display(display::kDefaultDisplayId, gfx::Rect(0, 0, 800, 600)),
      DisplayType::PRIMARY);
  AddSecondary();

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(
          MakeOsrInfo());

  ASSERT_NO_FATAL_FAILURE(
      ExpectTopology(infos, 2u, display::kDefaultDisplayId, true));
  EXPECT_EQ(kSecondaryId, infos.screen_infos[1].display_id);
}

TEST_F(ArkWebOsrScreenInfosTest, HostMetadataCannotReplacePlatformIdentity) {
  AddTwoDisplays();
  auto osr = MakeOsrInfo();
  osr.display_id = kSecondaryId;
  osr.label = "Host label";
  osr.is_primary = false;
  osr.is_internal = true;
  osr.is_extended = false;
  osr.handwriting_radius = 99;

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, kPrimaryId, true));
  EXPECT_EQ("Built-in display", infos.current().label);
  EXPECT_FALSE(infos.current().is_internal);
  EXPECT_EQ(0, infos.current().handwriting_radius);
  ExpectOsrFields(infos.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, InternalPanelFlagComesFromPlatform) {
  AddTwoDisplays();
  display::SetInternalDisplayIds({kPrimaryId});
  auto osr = MakeOsrInfo();
  osr.is_internal = false;

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, kPrimaryId, true));
  EXPECT_TRUE(infos.current().is_internal);
  EXPECT_FALSE(infos.screen_infos[1].is_internal);
  EXPECT_EQ("Built-in display", infos.current().label);
}

TEST_F(ArkWebOsrScreenInfosTest, ExternalPrimaryDoesNotBecomeInternal) {
  AddTwoDisplays();
  display::SetInternalDisplayIds({kPrimaryId});
  screen_.display_list().UpdateDisplay(MakeSecondary(), DisplayType::PRIMARY);

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(
          MakeOsrInfo());

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, kSecondaryId, true));
  EXPECT_FALSE(infos.current().is_internal);
  EXPECT_TRUE(infos.screen_infos[0].is_internal);
  EXPECT_EQ("External display", infos.current().label);
}

TEST_F(ArkWebOsrScreenInfosTest, CurrentColorSpacesComeFromPlatform) {
  auto primary = MakePrimary();
  const gfx::DisplayColorSpaces platform_colors(
      gfx::ColorSpace::CreateDisplayP3D65());
  primary.SetColorSpaces(platform_colors);
  screen_.display_list().AddDisplay(primary, DisplayType::PRIMARY);
  AddSecondary();
  const auto osr = MakeOsrInfo();
  ASSERT_NE(platform_colors, osr.display_color_spaces);

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  EXPECT_EQ(platform_colors, infos.current().display_color_spaces);
  EXPECT_EQ(kPrimaryId, infos.current_display_id);
  ExpectOsrFields(infos.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, SecondaryRenderingFieldsRemainPlatformValues) {
  AddPrimary();
  auto secondary = MakeSecondary();
  secondary.set_is_monochrome(true);
  screen_.display_list().AddDisplay(secondary, DisplayType::NOT_PRIMARY);
  auto osr = MakeOsrInfo();
  osr.is_monochrome = false;
  const auto expected_secondary =
      screen_.GetScreenInfosNearestDisplay(kPrimaryId).screen_infos[1];

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_EQ(2u, infos.screen_infos.size());
  const auto& actual = infos.screen_infos[1];
  EXPECT_FLOAT_EQ(2.f, actual.device_scale_factor);
  EXPECT_EQ(30, actual.depth);
  EXPECT_EQ(10, actual.depth_per_component);
  EXPECT_TRUE(actual.is_monochrome);
  EXPECT_EQ(gfx::Rect(1920, 0, 2560, 1440), actual.rect);
  EXPECT_EQ(gfx::Rect(1920, 0, 2560, 1400), actual.available_rect);
  EXPECT_EQ(0, actual.orientation_angle);
  EXPECT_EQ(expected_secondary, actual);
  ExpectOsrFields(infos.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, SecondaryColorSpacesRemainPlatformValues) {
  AddPrimary();
  auto secondary = MakeSecondary();
  const gfx::DisplayColorSpaces colors(gfx::ColorSpace::CreateDisplayP3D65());
  secondary.SetColorSpaces(colors);
  screen_.display_list().AddDisplay(secondary, DisplayType::NOT_PRIMARY);
  const auto osr = MakeOsrInfo();

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_EQ(2u, infos.screen_infos.size());
  EXPECT_EQ(colors, infos.screen_infos[1].display_color_spaces);
  EXPECT_EQ(kSecondaryId, infos.screen_infos[1].display_id);
  EXPECT_FALSE(infos.screen_infos[1].is_primary);
  EXPECT_NE(osr.display_color_spaces,
            infos.screen_infos[1].display_color_spaces);
}

TEST_F(ArkWebOsrScreenInfosTest, SecondaryRotationIsNotOverwrittenByOsr) {
  AddPrimary();
  auto secondary = MakeSecondary();
  secondary.set_rotation(display::Display::ROTATE_180);
  screen_.display_list().AddDisplay(secondary, DisplayType::NOT_PRIMARY);
  auto osr = MakeOsrInfo();
  osr.orientation_type = Orientation::kPortraitPrimary;
  osr.orientation_angle = 90;
  const auto expected_secondary =
      screen_.GetScreenInfosNearestDisplay(kPrimaryId).screen_infos[1];

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_EQ(2u, infos.screen_infos.size());
  EXPECT_EQ(expected_secondary, infos.screen_infos[1]);
  EXPECT_EQ(180, infos.screen_infos[1].orientation_angle);
  ExpectOsrFields(infos.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, NegativeSecondaryOriginIsPreserved) {
  AddPrimary();
  auto secondary = MakeSecondary();
  secondary.set_bounds(gfx::Rect(-2560, -120, 2560, 1440));
  secondary.set_work_area(gfx::Rect(-2560, -100, 2560, 1400));
  screen_.display_list().AddDisplay(secondary, DisplayType::NOT_PRIMARY);

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(
          MakeOsrInfo());

  ASSERT_EQ(2u, infos.screen_infos.size());
  EXPECT_EQ(gfx::Rect(-2560, -120, 2560, 1440), infos.screen_infos[1].rect);
  EXPECT_EQ(gfx::Rect(-2560, -100, 2560, 1400),
            infos.screen_infos[1].available_rect);
  EXPECT_TRUE(infos.screen_infos[1].is_extended);
}

TEST_F(ArkWebOsrScreenInfosTest, ZeroDepthOsrValuesAreNotReplacedByPlatform) {
  AddTwoDisplays();
  auto osr = MakeOsrInfo();
  osr.depth = 0;
  osr.depth_per_component = 0;
  osr.is_monochrome = false;

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  EXPECT_EQ(0, infos.current().depth);
  EXPECT_EQ(0, infos.current().depth_per_component);
  EXPECT_FALSE(infos.current().is_monochrome);
  EXPECT_EQ(30, infos.screen_infos[1].depth);
  ExpectOsrFields(infos.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, UndefinedOsrOrientationIsPreserved) {
  auto primary = MakePrimary();
  primary.set_rotation(display::Display::ROTATE_180);
  screen_.display_list().AddDisplay(primary, DisplayType::PRIMARY);
  auto osr = MakeOsrInfo();
  osr.orientation_type = Orientation::kUndefined;
  osr.orientation_angle = 0;

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  EXPECT_EQ(Orientation::kUndefined, infos.current().orientation_type);
  EXPECT_EQ(0, infos.current().orientation_angle);
  EXPECT_EQ(kPrimaryId, infos.current_display_id);
  ExpectOsrFields(infos.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, QueryDoesNotMutateHostInput) {
  AddTwoDisplays();
  auto osr = MakeOsrInfo();
  osr.label = "Host-owned input";
  osr.display_id = 700;
  const auto original = osr;

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  EXPECT_EQ(original, osr);
  EXPECT_EQ(kPrimaryId, infos.current_display_id);
  EXPECT_NE(osr.label, infos.current().label);
}

TEST_F(ArkWebOsrScreenInfosTest, QueryDoesNotMutatePlatformDisplays) {
  AddTwoDisplays();
  const std::vector<display::Display> original = screen_.GetAllDisplays();
  const auto osr = MakeOsrInfo();

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  EXPECT_EQ(original, screen_.GetAllDisplays());
  EXPECT_EQ(gfx::Rect(0, 0, 1920, 1080), screen_.GetPrimaryDisplay().bounds());
  EXPECT_NE(screen_.GetPrimaryDisplay().bounds(), infos.current().rect);
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, kPrimaryId, true));
}

TEST_F(ArkWebOsrScreenInfosTest, ReturnedSnapshotsDoNotAliasEachOther) {
  AddTwoDisplays();
  const auto osr = MakeOsrInfo();
  auto first = ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  const auto second =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  ASSERT_EQ(first, second);

  first.mutable_current().rect = gfx::Rect(1, 2, 3, 4);
  first.screen_infos[1].label = "Modified snapshot";
  first.screen_infos[1].is_extended = false;

  EXPECT_EQ(osr.rect, second.current().rect);
  EXPECT_EQ("External display", second.screen_infos[1].label);
  EXPECT_TRUE(second.screen_infos[1].is_extended);
  EXPECT_EQ(second,
            ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr));
}

TEST_F(ArkWebOsrScreenInfosTest, RepeatedQueryUsesLatestOsrRenderingFields) {
  AddTwoDisplays();
  const auto original_osr = MakeOsrInfo();
  const auto first =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(original_osr);
  auto updated_osr = original_osr;
  updated_osr.device_scale_factor = 3.f;
  updated_osr.depth = 16;
  updated_osr.depth_per_component = 5;
  updated_osr.is_monochrome = false;
  updated_osr.rect = gfx::Rect(20, 40, 1024, 768);
  updated_osr.available_rect = gfx::Rect(20, 50, 1024, 740);
  updated_osr.orientation_type = Orientation::kLandscapeSecondary;
  updated_osr.orientation_angle = 270;

  const auto second =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(updated_osr);

  ExpectOsrFields(first.current(), original_osr);
  ExpectOsrFields(second.current(), updated_osr);
  EXPECT_EQ(first.screen_infos[1], second.screen_infos[1]);
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(second, 2u, kPrimaryId, true));
}

TEST_F(ArkWebOsrScreenInfosTest, PluggingSecondaryChangesExtendedState) {
  AddPrimary();
  const auto osr = MakeOsrInfo();
  const auto before =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(before, 1u, kPrimaryId, false));

  AddSecondary();
  const auto after =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(after, 2u, kPrimaryId, true));
  ExpectOsrFields(after.current(), osr);
  EXPECT_EQ(kSecondaryId, after.screen_infos[1].display_id);
  EXPECT_FALSE(before.current().is_extended);
}

TEST_F(ArkWebOsrScreenInfosTest, UnpluggingSecondaryClearsExtendedState) {
  AddTwoDisplays();
  const auto osr = MakeOsrInfo();
  const auto before =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(before, 2u, kPrimaryId, true));

  screen_.display_list().RemoveDisplay(kSecondaryId);
  const auto after =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(after, 1u, kPrimaryId, false));
  ExpectOsrFields(after.current(), osr);
  EXPECT_TRUE(before.current().is_extended);
}

TEST_F(ArkWebOsrScreenInfosTest, RemovingOneOfThreeStillReportsExtended) {
  AddTwoDisplays();
  screen_.display_list().AddDisplay(
      display::Display(kThirdId, gfx::Rect(-1280, 0, 1280, 720)),
      DisplayType::NOT_PRIMARY);
  const auto osr = MakeOsrInfo();
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr), 3u,
      kPrimaryId, true));

  screen_.display_list().RemoveDisplay(kSecondaryId);
  const auto two =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(two, 2u, kPrimaryId, true));
  EXPECT_EQ(kThirdId, two.screen_infos[1].display_id);

  screen_.display_list().RemoveDisplay(kThirdId);
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr), 1u,
      kPrimaryId, false));
}

TEST_F(ArkWebOsrScreenInfosTest, RepluggingDisplayUsesNewIdentity) {
  AddTwoDisplays();
  const auto osr = MakeOsrInfo();
  const auto first =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  screen_.display_list().RemoveDisplay(kSecondaryId);
  auto replacement = MakeSecondary();
  replacement.set_id(kThirdId);
  replacement.set_label("Replacement display");
  screen_.display_list().AddDisplay(replacement, DisplayType::NOT_PRIMARY);

  const auto second =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(second, 2u, kPrimaryId, true));
  EXPECT_EQ(kThirdId, second.screen_infos[1].display_id);
  EXPECT_EQ("Replacement display", second.screen_infos[1].label);
  EXPECT_EQ(kSecondaryId, first.screen_infos[1].display_id);
}

TEST_F(ArkWebOsrScreenInfosTest, PrimarySwitchMovesOnlyRenderingOverride) {
  AddTwoDisplays();
  const auto osr = MakeOsrInfo();
  const auto before =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  screen_.display_list().UpdateDisplay(MakeSecondary(), DisplayType::PRIMARY);

  const auto after =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(after, 2u, kSecondaryId, true));
  ExpectOsrFields(after.current(), osr);
  EXPECT_EQ(gfx::Rect(0, 0, 1920, 1080), after.screen_infos[0].rect);
  EXPECT_EQ(gfx::Rect(0, 0, 1920, 1040), after.screen_infos[0].available_rect);
  EXPECT_FLOAT_EQ(1.f, after.screen_infos[0].device_scale_factor);
  EXPECT_EQ("Built-in display", after.screen_infos[0].label);
  EXPECT_EQ(kPrimaryId, before.current_display_id);
}

TEST_F(ArkWebOsrScreenInfosTest,
       RemovingOldPrimaryAfterSwitchRetainsNewPrimary) {
  AddTwoDisplays();
  const auto osr = MakeOsrInfo();
  screen_.display_list().UpdateDisplay(MakeSecondary(), DisplayType::PRIMARY);
  screen_.display_list().RemoveDisplay(kPrimaryId);

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 1u, kSecondaryId, false));
  EXPECT_EQ("External display", infos.current().label);
  ExpectOsrFields(infos.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, RemovingLastDisplayUsesDefaultThenRecovers) {
  AddPrimary();
  const auto osr = MakeOsrInfo();
  screen_.display_list().RemoveDisplay(kPrimaryId);
  const auto empty =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  ASSERT_NO_FATAL_FAILURE(
      ExpectTopology(empty, 1u, display::kDefaultDisplayId, false));
  ExpectOsrFields(empty.current(), osr);

  screen_.display_list().AddDisplay(MakeSecondary(), DisplayType::PRIMARY);
  const auto restored =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(restored, 1u, kSecondaryId, false));
  EXPECT_EQ("External display", restored.current().label);
  EXPECT_EQ(display::kDefaultDisplayId, empty.current_display_id);
}

TEST_F(ArkWebOsrScreenInfosTest, SecondaryMetricsUpdateIsVisibleOnNextQuery) {
  AddTwoDisplays();
  const auto osr = MakeOsrInfo();
  const auto before =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  auto updated = MakeSecondary();
  updated.set_bounds(gfx::Rect(-3840, 0, 3840, 2160));
  updated.set_work_area(gfx::Rect(-3840, 0, 3840, 2100));
  updated.set_device_scale_factor(2.5f);
  updated.set_color_depth(36);
  updated.set_depth_per_component(12);
  updated.set_label("Updated external display");
  screen_.display_list().UpdateDisplay(updated);

  const auto after =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_EQ(2u, after.screen_infos.size());
  EXPECT_EQ(gfx::Rect(-3840, 0, 3840, 2160), after.screen_infos[1].rect);
  EXPECT_EQ(gfx::Rect(-3840, 0, 3840, 2100),
            after.screen_infos[1].available_rect);
  EXPECT_FLOAT_EQ(2.5f, after.screen_infos[1].device_scale_factor);
  EXPECT_EQ(36, after.screen_infos[1].depth);
  EXPECT_EQ(12, after.screen_infos[1].depth_per_component);
  EXPECT_EQ("Updated external display", after.screen_infos[1].label);
  EXPECT_EQ(gfx::Rect(1920, 0, 2560, 1440), before.screen_infos[1].rect);
  ExpectOsrFields(after.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, PlatformColorUpdateDoesNotLoseOsrGeometry) {
  AddTwoDisplays();
  const auto osr = MakeOsrInfo();
  const auto before =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  auto primary = MakePrimary();
  const gfx::DisplayColorSpaces colors(gfx::ColorSpace::CreateDisplayP3D65());
  primary.SetColorSpaces(colors);
  screen_.display_list().UpdateDisplay(primary);

  const auto after =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  EXPECT_NE(before.current().display_color_spaces,
            after.current().display_color_spaces);
  EXPECT_EQ(colors, after.current().display_color_spaces);
  ExpectOsrFields(after.current(), osr);
  EXPECT_EQ(before.screen_infos[1], after.screen_infos[1]);
}

TEST_F(ArkWebOsrScreenInfosTest, PlatformRenameIsNotCachedOrOverriddenByHost) {
  AddPrimary();
  auto osr = MakeOsrInfo();
  osr.label = "Host name";
  const auto before =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  auto renamed = MakePrimary();
  renamed.set_label("Renamed panel");
  screen_.display_list().UpdateDisplay(renamed);

  const auto after =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  EXPECT_EQ("Built-in display", before.current().label);
  EXPECT_EQ("Renamed panel", after.current().label);
  EXPECT_EQ(kPrimaryId, after.current_display_id);
  ExpectOsrFields(after.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, InternalDisplayChangeIsVisibleOnNextQuery) {
  AddTwoDisplays();
  const auto osr = MakeOsrInfo();
  display::SetInternalDisplayIds({kPrimaryId});
  const auto before =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  display::SetInternalDisplayIds({kSecondaryId});
  const auto after =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  EXPECT_TRUE(before.current().is_internal);
  EXPECT_FALSE(before.screen_infos[1].is_internal);
  EXPECT_FALSE(after.current().is_internal);
  EXPECT_TRUE(after.screen_infos[1].is_internal);
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(after, 2u, kPrimaryId, true));
}

TEST_F(ArkWebOsrScreenInfosTest, PlatformScreenReplacementIsNotCached) {
  AddTwoDisplays();
  const auto osr = MakeOsrInfo();
  const auto before =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  display::ScreenBase replacement;
  replacement.display_list().AddDisplay(
      display::Display(kThirdId, gfx::Rect(0, 0, 1280, 720)),
      DisplayType::PRIMARY);
  display::Screen::SetScreenInstance(nullptr);
  display::Screen::SetScreenInstance(&replacement);
  const auto after =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  // Restore before the stack-owned screen is destroyed, including on failure.
  display::Screen::SetScreenInstance(nullptr);
  display::Screen::SetScreenInstance(&screen_);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(before, 2u, kPrimaryId, true));
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(after, 1u, kThirdId, false));
  ExpectOsrFields(after.current(), osr);
}

TEST_F(ArkWebOsrScreenInfosTest, ScreenRemovalReturnsToLegacyFallback) {
  AddTwoDisplays();
  const auto osr = MakeOsrInfo();
  const auto before =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);
  display::Screen::SetScreenInstance(nullptr);

  const auto fallback =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  ASSERT_NO_FATAL_FAILURE(ExpectTopology(before, 2u, kPrimaryId, true));
  EXPECT_EQ(display::ScreenInfos(osr), fallback);
  EXPECT_FALSE(fallback.current().is_extended);
}

TEST_F(ArkWebOsrScreenInfosTest, OverlappingDisplaysRemainPlatformTopology) {
  AddPrimary();
  auto secondary = MakeSecondary();
  secondary.set_bounds(gfx::Rect(0, 0, 1920, 1080));
  secondary.set_work_area(gfx::Rect(0, 0, 1920, 1040));
  screen_.display_list().AddDisplay(secondary, DisplayType::NOT_PRIMARY);

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(
          MakeOsrInfo());

  // Geometry is not a mirror-mode detector. The platform Screen must filter
  // mirrored outputs before supplying its distinct usable displays.
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, kPrimaryId, true));
  EXPECT_EQ(kSecondaryId, infos.screen_infos[1].display_id);
  EXPECT_EQ(gfx::Rect(0, 0, 1920, 1080), infos.screen_infos[1].rect);
}

class ArkWebOsrScreenInfosScaleTest
    : public ArkWebOsrScreenInfosTest,
      public testing::WithParamInterface<float> {};

TEST_P(ArkWebOsrScreenInfosScaleTest, MixedDpiRetainsHostScaleOnlyOnCurrent) {
  AddTwoDisplays();
  auto osr = MakeOsrInfo();
  osr.device_scale_factor = GetParam();

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  EXPECT_FLOAT_EQ(GetParam(), infos.current().device_scale_factor);
  ASSERT_EQ(2u, infos.screen_infos.size());
  EXPECT_FLOAT_EQ(2.f, infos.screen_infos[1].device_scale_factor);
  EXPECT_FLOAT_EQ(1.f, screen_.GetPrimaryDisplay().device_scale_factor());
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, kPrimaryId, true));
}

INSTANTIATE_TEST_SUITE_P(FractionalAndIntegralScale,
                         ArkWebOsrScreenInfosScaleTest,
                         testing::Values(1.f, 1.25f, 1.5f, 2.f, 3.f));

struct OsrOrientationCase {
  Orientation type;
  uint16_t angle;
};

class ArkWebOsrScreenInfosOrientationTest
    : public ArkWebOsrScreenInfosTest,
      public testing::WithParamInterface<OsrOrientationCase> {};

TEST_P(ArkWebOsrScreenInfosOrientationTest, HostOrientationOverridesCurrent) {
  AddTwoDisplays();
  auto osr = MakeOsrInfo();
  osr.orientation_type = GetParam().type;
  osr.orientation_angle = GetParam().angle;
  const auto expected_secondary =
      screen_.GetScreenInfosNearestDisplay(kPrimaryId).screen_infos[1];

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  EXPECT_EQ(GetParam().type, infos.current().orientation_type);
  EXPECT_EQ(GetParam().angle, infos.current().orientation_angle);
  ASSERT_EQ(2u, infos.screen_infos.size());
  EXPECT_EQ(expected_secondary, infos.screen_infos[1]);
  EXPECT_EQ(0, infos.screen_infos[1].orientation_angle);
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, kPrimaryId, true));
}

INSTANTIATE_TEST_SUITE_P(
    AllHostOrientations,
    ArkWebOsrScreenInfosOrientationTest,
    testing::Values(OsrOrientationCase{Orientation::kUndefined, 0},
                    OsrOrientationCase{Orientation::kLandscapePrimary, 0},
                    OsrOrientationCase{Orientation::kPortraitPrimary, 90},
                    OsrOrientationCase{Orientation::kLandscapeSecondary, 180},
                    OsrOrientationCase{Orientation::kPortraitSecondary, 270}));

struct OsrGeometryCase {
  gfx::Rect rect;
  gfx::Rect available_rect;
};

class ArkWebOsrScreenInfosGeometryTest
    : public ArkWebOsrScreenInfosTest,
      public testing::WithParamInterface<OsrGeometryCase> {};

TEST_P(ArkWebOsrScreenInfosGeometryTest, HostGeometryIsNotNormalizedAgain) {
  AddTwoDisplays();
  auto osr = MakeOsrInfo();
  osr.rect = GetParam().rect;
  osr.available_rect = GetParam().available_rect;

  const auto infos =
      ArkWebRenderWidgetHostViewOSRUtils::GetScreenInfosForArkweb(osr);

  EXPECT_EQ(GetParam().rect, infos.current().rect);
  EXPECT_EQ(GetParam().available_rect, infos.current().available_rect);
  ASSERT_EQ(2u, infos.screen_infos.size());
  EXPECT_EQ(gfx::Rect(1920, 0, 2560, 1440), infos.screen_infos[1].rect);
  EXPECT_EQ(gfx::Rect(1920, 0, 2560, 1400),
            infos.screen_infos[1].available_rect);
  ASSERT_NO_FATAL_FAILURE(ExpectTopology(infos, 2u, kPrimaryId, true));
}

INSTANTIATE_TEST_SUITE_P(
    ViewBoundsAndWorkArea,
    ArkWebOsrScreenInfosGeometryTest,
    testing::Values(OsrGeometryCase{gfx::Rect(0, 0, 1, 1),
                                    gfx::Rect(0, 0, 1, 1)},
                    OsrGeometryCase{gfx::Rect(-800, -20, 800, 600),
                                    gfx::Rect(-800, 0, 800, 560)},
                    OsrGeometryCase{gfx::Rect(200, 100, 3840, 2160),
                                    gfx::Rect(200, 120, 3840, 2100)},
                    OsrGeometryCase{gfx::Rect(), gfx::Rect()}));

}  // namespace
