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
#include <optional>
#include <string_view>

#include <Eigen/Core>

#include <input_config_generated.h>
#include <settings_generated.h>
#include <z13/components/input_state.h>
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
};

std::optional<z13::fbs::input::Keycode> ButtonKey(TouchControl control);

Eigen::Vector2f ButtonCenter(const Layout& layout, TouchControl control, const Eigen::Vector2f& screen_size);
float ButtonRadius(const Layout& layout, const Eigen::Vector2f& screen_size);
float StickRadius(const Layout& layout, const Eigen::Vector2f& screen_size);

// A button under `position`, else the stick in its zone, else the look zone.
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
