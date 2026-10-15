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

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include <Eigen/Core>
#include <Eigen/Geometry>

#include <input_config_generated.h>
#include <settings_generated.h>
#include <z13/components/input_state.h>
#include <z13/components/hotbar.h>
#include <z13/components/touch_controls.h>

// How touches become input (docs/android-plan.md, "Управление"): the floating stick, the look
// zone and the buttons, laid out by TouchLayout. Positions and sizes are in pixels.
namespace z13::input::touch {

using Layout = z13::fbs::settings::TouchLayoutT;

struct TouchButton {
  TouchControl control {};
  z13::fbs::input::Keycode key {};
  std::string_view label;
};

constexpr std::array kTouchButtons = {
    TouchButton {TouchControl::kJump, z13::fbs::input::Keycode::TOUCH_JUMP, "Jump"},
    TouchButton {TouchControl::kCrouch, z13::fbs::input::Keycode::TOUCH_CROUCH, "Crouch"},
    TouchButton {TouchControl::kUse, z13::fbs::input::Keycode::TOUCH_USE, "Use"},
    TouchButton {TouchControl::kFlashlight, z13::fbs::input::Keycode::TOUCH_FLASHLIGHT, "Light"},
    // Routed only while a build tool is in hand (the Building group); the HUD labels the
    // primary one with the tool's mode.
    TouchButton {TouchControl::kToolMode, z13::fbs::input::Keycode::TOUCH_TOOL_MODE, "Mode"},
    TouchButton {TouchControl::kPrimary, z13::fbs::input::Keycode::TOUCH_PRIMARY, "Tool"},
};

// The hotbar's cells, left to right, and the key each one presses.
constexpr std::array kHotbarCellButtons = {
    TouchButton {TouchControl::kCell1, z13::fbs::input::Keycode::TOUCH_CELL_1, "1"},
    TouchButton {TouchControl::kCell2, z13::fbs::input::Keycode::TOUCH_CELL_2, "2"},
    TouchButton {TouchControl::kCell3, z13::fbs::input::Keycode::TOUCH_CELL_3, "3"},
    TouchButton {TouchControl::kCell4, z13::fbs::input::Keycode::TOUCH_CELL_4, "4"},
    TouchButton {TouchControl::kCell5, z13::fbs::input::Keycode::TOUCH_CELL_5, "5"},
    TouchButton {TouchControl::kCell6, z13::fbs::input::Keycode::TOUCH_CELL_6, "6"},
    TouchButton {TouchControl::kCell7, z13::fbs::input::Keycode::TOUCH_CELL_7, "7"},
    TouchButton {TouchControl::kCell8, z13::fbs::input::Keycode::TOUCH_CELL_8, "8"},
    TouchButton {TouchControl::kCell9, z13::fbs::input::Keycode::TOUCH_CELL_9, "9"},
};
static_assert(kHotbarCellButtons.size() == z13::inventory::kHotbarCells);

std::optional<z13::fbs::input::Keycode> ButtonKey(TouchControl control);

Eigen::Vector2f ButtonCenter(const Layout& layout, TouchControl control, const Eigen::Vector2f& screen_size);
float ButtonRadius(const Layout& layout, const Eigen::Vector2f& screen_size);
// The hotbar cell `cell` (from 0), in pixels; the HUD draws the hotbar there on PC too.
Eigen::AlignedBox2f CellRect(const Layout& layout, size_t cell, const Eigen::Vector2f& screen_size);
float StickRadius(const Layout& layout, const Eigen::Vector2f& screen_size);

// A hotbar cell or a button under `position`, else the stick in its zone, else the look zone.
TouchControl ControlAt(const Layout& layout, const Eigen::Vector2f& position, const Eigen::Vector2f& screen_size);

// The stick finger's deflection: x right, y forward, each in [-1, 1] and in steps of
// layout.stick_step, zero inside the dead zone.
Eigen::Vector2f StickValue(const Layout& layout, const TouchFinger& finger, const Eigen::Vector2f& screen_size);

std::array<KeyTransition, 4> StickKeys(const Eigen::Vector2f& value);

// Yaw and pitch degrees a finger moving by `delta` turns the view, the way a mouse does.
Eigen::Vector2f LookDegrees(const Layout& layout, const Eigen::Vector2f& delta, const Eigen::Vector2f& screen_size);

// Queues `value` for `key` unless it already is the key's last value. A new deflection replaces
// one still queued, so a sweeping finger doesn't fall behind; a press and a release always stay.
void ScheduleKey(InputState& input_state, z13::fbs::input::Keycode key, float value);

}  // namespace z13::input::touch
