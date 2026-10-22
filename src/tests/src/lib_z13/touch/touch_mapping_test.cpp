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

#include <Eigen/Dense>

#include <touch/touch_mapping.h>

namespace z13::input::touch {
namespace {

using z13::fbs::input::Keycode;

const Eigen::Vector2f kScreen(1000.f, 500.f);
constexpr float kEpsilon = 1e-5f;

// A stick finger that touched mid-zone and moved by `offset` stick radii.
TouchFinger StickFinger(const Layout& layout, const Eigen::Vector2f& offset) {
  const Eigen::Vector2f origin(200.f, 250.f);
  return {.control = TouchControl::kStick,
          .origin = origin,
          .position = origin + offset * StickRadius(layout, kScreen)};
}

float Scheduled(const InputState& state, Keycode key) {
  return state.input_state[KeyIndex(key)];
}

TEST(TouchMappingTest, ControlAtPicksButtonThenZone) {
  const Layout layout;
  const TouchLayer walk = TouchLayer::kWalk;
  EXPECT_EQ(ControlAt(layout, walk, ButtonCenter(layout, walk, TouchControl::kJump, kScreen), kScreen),
            TouchControl::kJump);
  EXPECT_EQ(ControlAt(layout, walk, {100.f, 250.f}, kScreen), TouchControl::kStick);
  EXPECT_EQ(ControlAt(layout, walk, {600.f, 250.f}, kScreen), TouchControl::kLook);
}

TEST(TouchMappingTest, TheToolsButtonsShowOnlyWithATool) {
  for (const TouchControl control : kToolButtons) {
    EXPECT_FALSE(InLayer(TouchLayer::kWalk, control));
    EXPECT_TRUE(InLayer(TouchLayer::kBlock, control));
    EXPECT_TRUE(InLayer(TouchLayer::kWall, control));
    EXPECT_TRUE(InLayer(TouchLayer::kMerge, control));
    EXPECT_TRUE(InLayer(TouchLayer::kBridge, control));
  }
  EXPECT_TRUE(InLayer(TouchLayer::kWalk, TouchControl::kJump));
  EXPECT_TRUE(InLayer(TouchLayer::kWall, TouchControl::kJump));
}

TEST(TouchMappingTest, APanelButtonAnswersOnlyInItsLayer) {
  const Layout layout;
  EXPECT_TRUE(InLayer(TouchLayer::kBlock, TouchControl::kTurnZ));
  EXPECT_FALSE(InLayer(TouchLayer::kWall, TouchControl::kTurnZ));
  EXPECT_TRUE(InLayer(TouchLayer::kWall, TouchControl::kThicker));
  EXPECT_FALSE(InLayer(TouchLayer::kBlock, TouchControl::kThicker));
  EXPECT_TRUE(InLayer(TouchLayer::kMerge, TouchControl::kMergeAt));
  EXPECT_FALSE(InLayer(TouchLayer::kWall, TouchControl::kMergeAt));
  EXPECT_TRUE(InLayer(TouchLayer::kBridge, TouchControl::kFlipDiagonal));
  EXPECT_TRUE(InLayer(TouchLayer::kBridge, TouchControl::kThicker));
  EXPECT_FALSE(InLayer(TouchLayer::kWall, TouchControl::kFlipDiagonal));

  // The wall layer's first button sits where the block layer's first does, so a hidden one
  // doesn't catch a touch.
  const Eigen::Vector2f first = ButtonCenter(layout, TouchLayer::kWall, TouchControl::kThicker, kScreen);
  EXPECT_EQ(ControlAt(layout, TouchLayer::kWall, first, kScreen), TouchControl::kThicker);
  EXPECT_NE(ControlAt(layout, TouchLayer::kBlock, first, kScreen), TouchControl::kThicker);
  const Eigen::Vector2f second = ButtonCenter(layout, TouchLayer::kBlock, TouchControl::kNextPrimitive, kScreen);
  EXPECT_NE(ControlAt(layout, TouchLayer::kWall, second, kScreen), TouchControl::kNextPrimitive);
}

TEST(TouchMappingTest, ALayerOutsideTheEnumHasNoPanel) {
  const TouchLayer unknown = static_cast<TouchLayer>(99);

  EXPECT_TRUE(PanelOf(unknown).empty());
  EXPECT_FALSE(InLayer(unknown, TouchControl::kThicker));
  EXPECT_TRUE(InLayer(unknown, TouchControl::kJump) == InLayer(TouchLayer::kWalk, TouchControl::kJump));
}

TEST(TouchMappingTest, EveryPanelButtonHasAKey) {
  for (const TouchLayer layer : {TouchLayer::kBlock, TouchLayer::kWall, TouchLayer::kMerge, TouchLayer::kBridge}) {
    EXPECT_FALSE(PanelOf(layer).empty());
    for (const TouchControl control : PanelOf(layer)) {
      EXPECT_TRUE(ButtonKey(control).has_value());
    }
  }
}

TEST(TouchMappingTest, StickInsideDeadZoneIsZero) {
  const Layout layout;
  const Eigen::Vector2f value = StickValue(layout, StickFinger(layout, {layout.stick_dead_zone * 0.9f, 0.f}), kScreen);
  EXPECT_TRUE(value.isZero());
}

TEST(TouchMappingTest, StickFullUpIsForwardAndClamped) {
  const Layout layout;
  // Screen y grows downwards: up is a negative offset, and twice the radius still gives 1.
  const Eigen::Vector2f value = StickValue(layout, StickFinger(layout, {0.f, -2.f}), kScreen);
  EXPECT_NEAR(value.x(), 0.f, kEpsilon);
  EXPECT_NEAR(value.y(), 1.f, kEpsilon);
}

TEST(TouchMappingTest, StickValueMovesInSteps) {
  Layout layout;
  layout.stick_dead_zone = 0.f;
  layout.stick_step = 0.25f;
  const Eigen::Vector2f value = StickValue(layout, StickFinger(layout, {0.4f, 0.f}), kScreen);
  EXPECT_NEAR(value.x(), 0.5f, kEpsilon);
  EXPECT_NEAR(value.y(), 0.f, kEpsilon);
}

TEST(TouchMappingTest, StickKeysSplitTheAxes) {
  const auto keys = StickKeys({-0.5f, 0.75f});
  EXPECT_EQ(keys[0].key, Keycode::TOUCH_STICK_UP);
  EXPECT_NEAR(keys[0].value, 0.75f, kEpsilon);
  EXPECT_NEAR(keys[1].value, 0.f, kEpsilon);
  EXPECT_NEAR(keys[2].value, 0.5f, kEpsilon);
  EXPECT_NEAR(keys[3].value, 0.f, kEpsilon);
}

TEST(TouchMappingTest, LookTurnsLikeAMouse) {
  const Layout layout;
  // Dragging right by half the screen's height turns right (negative yaw) by half look_degrees.
  const Eigen::Vector2f degrees = LookDegrees(layout, {kScreen.y() / 2.f, 0.f}, kScreen);
  EXPECT_NEAR(degrees.x(), -layout.look_degrees / 2.f, kEpsilon);
  EXPECT_NEAR(degrees.y(), 0.f, kEpsilon);
}

TEST(TouchMappingTest, ScheduleKeyKeepsPressAndRelease) {
  InputState state;
  ScheduleKey(state, Keycode::TOUCH_JUMP, 1.f);
  ScheduleKey(state, Keycode::TOUCH_JUMP, 1.f);
  ScheduleKey(state, Keycode::TOUCH_JUMP, 0.f);
  ASSERT_EQ(state.pending.size(), 2U);
  EXPECT_EQ(state.pending[0].value, 1.f);
  EXPECT_EQ(state.pending[1].value, 0.f);
  EXPECT_EQ(Scheduled(state, Keycode::TOUCH_JUMP), 0.f);
}

TEST(TouchMappingTest, ScheduleKeyReplacesAQueuedDeflection) {
  InputState state;
  ScheduleKey(state, Keycode::TOUCH_STICK_UP, 0.25f);
  ScheduleKey(state, Keycode::TOUCH_STICK_UP, 0.5f);
  ScheduleKey(state, Keycode::TOUCH_STICK_UP, 0.75f);
  ASSERT_EQ(state.pending.size(), 1U);
  EXPECT_EQ(state.pending[0].value, 0.75f);
}

TEST(TouchMappingTest, EachHotbarCellPressesItsOwnKey) {
  const Layout layout;
  for (size_t cell = 0; cell < kHotbarCellButtons.size(); ++cell) {
    const TouchControl control = ControlAt(layout, TouchLayer::kBlock, CellRect(layout, cell, kScreen).center(), kScreen);
    EXPECT_EQ(control, kHotbarCellButtons[cell].control) << cell;
    EXPECT_EQ(ButtonKey(control), kHotbarCellButtons[cell].key) << cell;
  }
}

TEST(TouchMappingTest, TheHotbarCellsSitInARowAndClearOfTheButtons) {
  const Layout layout;
  for (size_t cell = 1; cell < kHotbarCellButtons.size(); ++cell) {
    EXPECT_NEAR(CellRect(layout, cell, kScreen).min().x(), CellRect(layout, cell - 1, kScreen).max().x(), kEpsilon);
  }
  for (const TouchLayer layer :
       {TouchLayer::kWalk, TouchLayer::kBlock, TouchLayer::kWall, TouchLayer::kMerge, TouchLayer::kBridge}) {
    for (const TouchButton& button : kTouchButtons) {
      if (InLayer(layer, button.control)) {
        EXPECT_EQ(ControlAt(layout, layer, ButtonCenter(layout, layer, button.control, kScreen), kScreen),
                  button.control)
            << button.label;
      }
    }
  }
}

}  // namespace
}  // namespace z13::input::touch
