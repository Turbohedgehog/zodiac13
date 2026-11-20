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

#include <string>

#include <z13/components/editor/editor_camera.h>
#include <player/gameplay_entities.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/input/input_events.h>
#include <z13_settings/settings.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "editor_test_support.h"

namespace z13::station_editor {
namespace {

using z13::editor::EditorCamera;
using z13::fbs::input::Keycode;
using z13::testing::Z13TestWorld;

constexpr std::string_view kCommand = "editor_camera";
constexpr std::string_view kScene = "prototype";
constexpr float kDragPixels = 20.f;
constexpr float kFingerGapPixels = 40.f;
constexpr float kFingerSpreadPixels = 80.f;
constexpr float kScreenWidth = 1000.f;
constexpr float kScreenHeight = 600.f;
constexpr int kWalkTicks = 10;
constexpr int kCenterX = 500;
constexpr int kCenterY = 300;

rfl::Generic Str(std::string value) {
  return rfl::Generic(std::move(value));
}

const EditorCamera& Camera(Z13TestWorld& test_world) {
  return test_world.World().get<EditorCamera>();
}

z13::input::MouseMoveEvent Drag(int pixels) {
  return {.delta = {pixels, pixels}};
}

z13::input::TouchEvent Finger(std::uint64_t id, float x) {
  return {.finger = id, .position = Eigen::Vector2f(x, 0.f), .screen_size = Eigen::Vector2f(1000.f, 600.f)};
}

class EditorCameraTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_world_.Tick();
    ASSERT_TRUE(RunCommand(test_world_, "editor_load", {{"name", Str(std::string(kScene))}}));
  }

  void SwitchOn() {
    ASSERT_TRUE(RunCommand(test_world_, kCommand));
    test_world_.World().get_mut<EditorCamera>().screen_pixels = Eigen::Vector2f(kScreenWidth, kScreenHeight);
  }

  CommandResult PickAt(int x, int y) {
    return RunCommand(test_world_, "editor_pick_at", {{"x", rfl::Generic(x)}, {"y", rfl::Generic(y)}});
  }

  bool Selected() { return test_world_.World().get<z13::editor::EditorView>().selected.has_value(); }

  Z13TestWorld test_world_;
};

TEST_F(EditorCameraTest, SwitchingOnFramesTheGraphAndFreesTheCursorAndOffGivesItBack) {
  const float start_distance = Camera(test_world_).orbit.distance;

  SwitchOn();

  EXPECT_TRUE(Camera(test_world_).active);
  EXPECT_TRUE(test_world_.World().has<z13::gameplay::FreeCursor>());
  EXPECT_FALSE(Camera(test_world_).orbit.target.isZero());
  EXPECT_NE(Camera(test_world_).orbit.distance, start_distance);

  ASSERT_TRUE(RunCommand(test_world_, kCommand));
  EXPECT_FALSE(Camera(test_world_).active);
  EXPECT_FALSE(test_world_.World().has<z13::gameplay::FreeCursor>());
}

TEST_F(EditorCameraTest, TheSliceStepsThroughTheDecksOnlyWhileTheCameraIsOn) {
  EXPECT_FALSE(RunCommand(test_world_, "editor_slice", {{"step", rfl::Generic(-1)}}));

  SwitchOn();
  ASSERT_TRUE(RunCommand(test_world_, "editor_slice", {{"step", rfl::Generic(-1)}}));
  EXPECT_TRUE(Camera(test_world_).slice_cells);

  ASSERT_TRUE(RunCommand(test_world_, "editor_slice", {{"step", rfl::Generic(1)}}));
  EXPECT_FALSE(Camera(test_world_).slice_cells);
}

TEST_F(EditorCameraTest, TheCursorStaysFreeWhileTheCameraIsOn) {
  SwitchOn();

  test_world_.Tick();

  EXPECT_TRUE(test_world_.World().has<z13::gameplay::FreeCursor>());
}

TEST_F(EditorCameraTest, TheWheelZoomsAndOnlyWhileTheCameraIsOn) {
  const float before = Camera(test_world_).orbit.distance;
  test_world_.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_WHEEL_UP));
  EXPECT_FLOAT_EQ(Camera(test_world_).orbit.distance, before);

  SwitchOn();
  const float framed = Camera(test_world_).orbit.distance;
  test_world_.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_WHEEL_UP));
  EXPECT_LT(Camera(test_world_).orbit.distance, framed);
  test_world_.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_WHEEL_DOWN));
  EXPECT_NEAR(Camera(test_world_).orbit.distance, framed, framed * 1e-4f);
}

TEST_F(EditorCameraTest, ThePlayerStandsStillWhileTheCameraIsOnAndWalksAgainWhenItIsOff) {
  const auto id = test_world_.World().get<z13::gameplay::LocalPlayer>().id;
  const flecs::entity player = test_world_.World().lookup(z13::gameplay::PlayerEntityName(id.value()).c_str());
  const auto walk = [&] {
    const Eigen::Vector3f start = player.get<Eigen::Matrix4f>().block<3, 1>(0, 3);
    test_world_.EmitInput(z13::testing::KeyDown(Keycode::KEY_W));
    for (int i = 0; i < kWalkTicks; ++i) {
      test_world_.Tick();
    }
    test_world_.EmitInput(z13::testing::KeyUp(Keycode::KEY_W));
    test_world_.Tick();
    return (player.get<Eigen::Matrix4f>().block<3, 1>(0, 3) - start).norm();
  };

  SwitchOn();
  EXPECT_FLOAT_EQ(walk(), 0.f);

  ASSERT_TRUE(RunCommand(test_world_, kCommand));
  EXPECT_GT(walk(), 0.f);
}

TEST_F(EditorCameraTest, AWheelNotchOfZeroZoomsNothing) {
  SwitchOn();
  test_world_.World().get_mut<z13::EditorCameraTuning>().zoom_per_notch = 0.f;
  const float framed = Camera(test_world_).orbit.distance;

  test_world_.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_WHEEL_UP));
  test_world_.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_WHEEL_DOWN));

  EXPECT_FLOAT_EQ(Camera(test_world_).orbit.distance, framed);
}

TEST_F(EditorCameraTest, TheRightButtonPansAndTheMiddleOneOrbits) {
  SwitchOn();
  const auto start = Camera(test_world_).orbit;

  test_world_.EmitInput(Drag(static_cast<int>(kDragPixels)));
  EXPECT_TRUE(Camera(test_world_).orbit.target.isApprox(start.target));

  test_world_.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_BUTTON_RIGHT));
  test_world_.EmitInput(Drag(static_cast<int>(kDragPixels)));
  test_world_.EmitInput(z13::testing::MouseUp(Keycode::MOUSE_BUTTON_RIGHT));
  EXPECT_FALSE(Camera(test_world_).orbit.target.isApprox(start.target));
  EXPECT_FLOAT_EQ(Camera(test_world_).orbit.yaw_degrees, start.yaw_degrees);

  const auto panned = Camera(test_world_).orbit;
  test_world_.EmitInput(z13::testing::MouseDown(Keycode::MOUSE_BUTTON_MIDDLE));
  test_world_.EmitInput(Drag(static_cast<int>(kDragPixels)));
  test_world_.EmitInput(z13::testing::MouseUp(Keycode::MOUSE_BUTTON_MIDDLE));
  EXPECT_NE(Camera(test_world_).orbit.yaw_degrees, panned.yaw_degrees);
  EXPECT_TRUE(Camera(test_world_).orbit.target.isApprox(panned.target));

  test_world_.EmitInput(Drag(static_cast<int>(kDragPixels)));
  EXPECT_FALSE(Camera(test_world_).orbit_held);
}

TEST_F(EditorCameraTest, TwoFingersMovingApartZoomIn) {
  SwitchOn();
  const float before = Camera(test_world_).orbit.distance;

  test_world_.EmitInput(z13::input::TouchDownEvent {Finger(1, 0.f)});
  test_world_.EmitInput(z13::input::TouchDownEvent {Finger(2, kFingerGapPixels)});
  test_world_.EmitInput(z13::input::TouchMoveEvent {Finger(2, kFingerSpreadPixels)});

  EXPECT_LT(Camera(test_world_).orbit.distance, before);

  test_world_.EmitInput(z13::input::TouchUpEvent {Finger(1, 0.f)});
  test_world_.EmitInput(z13::input::TouchUpEvent {Finger(2, kFingerSpreadPixels)});
  EXPECT_TRUE(Camera(test_world_).fingers.empty());
}

TEST_F(EditorCameraTest, ThePixelAtTheMiddleOfTheScreenPicksWhatTheCameraLooksAt) {
  SwitchOn();

  EXPECT_TRUE(PickAt(kCenterX, kCenterY));
  EXPECT_TRUE(Selected());
}

TEST_F(EditorCameraTest, APixelOnTheSkyPicksNothingAndTheCameraMustBeOn) {
  EXPECT_FALSE(PickAt(kCenterX, kCenterY));

  SwitchOn();
  const auto sky = PickAt(0, 0);

  EXPECT_FALSE(sky);
  EXPECT_FALSE(Selected());
}

TEST_F(EditorCameraTest, ALeftClickPicksUnderTheCursorAndOtherButtonsDoNot) {
  SwitchOn();
  z13::input::MouseButtonEvent click {.pos = {kCenterX, kCenterY}, .button = Keycode::MOUSE_BUTTON_RIGHT, .clicks = 1};

  test_world_.EmitInput(z13::input::MouseButtonDownEvent {click});
  test_world_.Tick();
  EXPECT_FALSE(Selected());

  click.button = Keycode::MOUSE_BUTTON_LEFT;
  test_world_.EmitInput(z13::input::MouseButtonDownEvent {click});
  test_world_.Tick();
  EXPECT_TRUE(Selected());
}

}  // namespace
}  // namespace z13::station_editor
