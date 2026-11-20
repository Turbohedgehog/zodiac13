/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include <z13/components/editor/editor_camera.h>
#include <z13/components/editor/editor_drag.h>
#include <z13/components/editor/editor_view.h>
#include <z13/components/input/input_events.h>
#include <z13/components/station/overlay_layers.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "editor_test_support.h"

namespace z13::station_editor {
namespace {

using z13::editor::EditorCamera;
using z13::editor::EditorDrag;
using z13::editor::EditorView;
using z13::fbs::input::Keycode;
using z13::testing::Z13TestWorld;

constexpr std::string_view kScene = "prototype";
constexpr std::string_view kLayerName = "graph";
constexpr float kScreenWidth = 1000.f;
constexpr float kScreenHeight = 600.f;
constexpr int kCenterX = 500;
constexpr int kCenterY = 300;
constexpr int kDragPixels = 100;

rfl::Generic Str(std::string value) {
  return rfl::Generic(std::move(value));
}

class EditorDragTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_world_.Tick();
    ASSERT_TRUE(RunCommand(test_world_, "editor_load", {{"name", Str(std::string(kScene))}}));
    ASSERT_TRUE(RunCommand(test_world_, "editor_window"));
    ASSERT_TRUE(RunCommand(test_world_, "editor_camera"));
    test_world_.World().get_mut<EditorCamera>().screen_pixels = Eigen::Vector2f(kScreenWidth, kScreenHeight);
    test_world_.World().get_mut<z13::station::OverlayLayers>().shown = std::string(kLayerName);
  }

  void Press(int x, int y) {
    z13::input::MouseButtonEvent click {.pos = {x, y}, .button = Keycode::MOUSE_BUTTON_LEFT, .clicks = 1};
    test_world_.EmitInput(z13::input::MouseButtonDownEvent {click});
    test_world_.Tick();
  }

  void Release(int x, int y) {
    z13::input::MouseButtonEvent click {.pos = {x, y}, .button = Keycode::MOUSE_BUTTON_LEFT, .clicks = 1};
    test_world_.EmitInput(z13::input::MouseButtonUpEvent {click});
    test_world_.Tick();
    test_world_.Tick();
  }

  const EditorDrag& Drag() { return test_world_.World().get<EditorDrag>(); }

  const z13::editor::EditorItemView* Picked() {
    const EditorView& view = test_world_.World().get<EditorView>();
    if (!view.selected) {
      return nullptr;
    }
    const auto found = std::ranges::find(view.rooms, view.selected->id, &z13::editor::EditorItemView::id);
    return found != view.rooms.end() ? &*found : nullptr;
  }

  Z13TestWorld test_world_;
};

TEST_F(EditorDragTest, DraggingThePickedRoomPreviewsWhereItLandsAndTheReleaseMovesIt) {
  Press(kCenterX, kCenterY);
  ASSERT_NE(Picked(), nullptr);
  const std::string id = Picked()->id;
  const Eigen::Vector3i before = Picked()->box->origin;
  const std::size_t boxes = test_world_.World().get<z13::station::OverlayLayers>().Find(std::string(kLayerName))->boxes.size();

  test_world_.EmitInput(z13::input::MousePos {kCenterX + kDragPixels, kCenterY});
  test_world_.Tick();

  EXPECT_TRUE(Drag().active);
  EXPECT_EQ(Drag().room, id);
  EXPECT_FALSE(Drag().offset_cells.isZero());
  EXPECT_EQ(test_world_.World().get<z13::station::OverlayLayers>().Find(std::string(kLayerName))->boxes.size(), boxes + 1);

  const Eigen::Vector2i offset = Drag().offset_cells;
  Release(kCenterX + kDragPixels, kCenterY);

  EXPECT_FALSE(Drag().active);
  ASSERT_NE(Picked(), nullptr);
  EXPECT_EQ(Picked()->id, id);
  EXPECT_EQ(Picked()->box->origin, Eigen::Vector3i(before.x() + offset.x(), before.y() + offset.y(), before.z()));
}

TEST_F(EditorDragTest, AReleaseWithoutMovingChangesNothing) {
  Press(kCenterX, kCenterY);
  ASSERT_NE(Picked(), nullptr);
  const Eigen::Vector3i before = Picked()->box->origin;

  Release(kCenterX, kCenterY);

  ASSERT_NE(Picked(), nullptr);
  EXPECT_EQ(Picked()->box->origin, before);
}

TEST_F(EditorDragTest, APressOnTheSkyStartsNoDragOfTheOldSelection) {
  Press(kCenterX, kCenterY);
  ASSERT_NE(Picked(), nullptr);

  Press(0, 0);
  test_world_.EmitInput(z13::input::MousePos {kDragPixels, kDragPixels});

  EXPECT_FALSE(Drag().active);
}

}  // namespace
}  // namespace z13::station_editor
