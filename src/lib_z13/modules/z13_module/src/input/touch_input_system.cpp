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

#include "touch_input_system.h"

#include <algorithm>

#include <flecs.h>

#include <lib_core/state/world_state.h>
#include <lib_core/world/lifecycle.h>
#include <z13/components/gameplay.h>
#include <z13/components/input_config.h>
#include <z13/components/input_events.h>
#include <z13/components/input_state.h>
#include <z13/components/touch_controls.h>
#include <z13_settings/settings.h>
#include <touch/touch_mapping.h>

namespace z13::gameplay::input {

namespace {

using z13::input::TouchControl;
using z13::input::TouchControls;
using z13::input::TouchFinger;

void OnTouchDown(const z13::input::TouchDownEvent& event, TouchControls& controls, const z13::TouchLayout& layout,
                 z13::input::InputState& input_state) {
  controls.used = true;
  controls.screen_size = event.screen_size;
  if (controls.stats_area && controls.stats_area->contains(event.position.cwiseQuotient(event.screen_size))) {
    controls.stats_shown = !controls.stats_shown;
    controls.fingers.push_back({.finger = event.finger, .control = TouchControl::kStats, .origin = event.position,
                                .position = event.position});
    return;
  }
  TouchControl control = z13::input::touch::ControlAt(layout, event.position, event.screen_size);
  // One stick: another finger in its zone looks.
  if (control == TouchControl::kStick && std::ranges::contains(controls.fingers, control, &TouchFinger::control)) {
    control = TouchControl::kLook;
  }
  if (const auto key = z13::input::touch::ButtonKey(control)) {
    z13::input::touch::ScheduleKey(input_state, *key, 1.f);
  }
  controls.fingers.push_back(
      {.finger = event.finger, .control = control, .origin = event.position, .position = event.position});
}

void OnTouchMove(const z13::input::TouchMoveEvent& event, TouchControls& controls, const z13::TouchLayout& layout,
                 const z13::input::InputConfig& input_config, z13::input::InputState& input_state) {
  const auto finger = std::ranges::find(controls.fingers, event.finger, &TouchFinger::finger);
  if (finger == controls.fingers.end()) {
    return;
  }
  const Eigen::Vector2f delta = event.position - finger->position;
  finger->position = event.position;
  if (finger->control == TouchControl::kStats) {
    return;
  }
  if (finger->control == TouchControl::kStick) {
    for (const auto& [key, value] :
         z13::input::touch::StickKeys(z13::input::touch::StickValue(layout, *finger, event.screen_size))) {
      z13::input::touch::ScheduleKey(input_state, key, value);
    }
    return;
  }
  // A button's finger looks as it slides on, like a shooter's fire button (docs/android-plan.md).
  const Eigen::Vector2f degrees = z13::input::touch::LookDegrees(layout, delta, event.screen_size);
  input_state.mouse_yaw_delta_deg += input_config.invert_x ? -degrees.x() : degrees.x();
  input_state.mouse_pitch_delta_deg += input_config.invert_y ? -degrees.y() : degrees.y();
}

void OnTouchUp(const z13::input::TouchUpEvent& event, TouchControls& controls, z13::input::InputState& input_state) {
  const auto finger = std::ranges::find(controls.fingers, event.finger, &TouchFinger::finger);
  if (finger == controls.fingers.end()) {
    return;
  }
  if (finger->control == TouchControl::kStick) {
    for (const auto& [key, value] : z13::input::touch::StickKeys(Eigen::Vector2f::Zero())) {
      z13::input::touch::ScheduleKey(input_state, key, value);
    }
  } else if (const auto key = z13::input::touch::ButtonKey(finger->control)) {
    z13::input::touch::ScheduleKey(input_state, *key, 0.f);
  }
  controls.fingers.erase(finger);
}

void RegisterSystems(flecs::world world) {
  // The launcher overwrites this with the loaded settings (z13::InstallSettings).
  world.set<z13::TouchLayout>({});
  world.set<TouchControls>({});

  // A touch on the pause menu works the menu; moves and releases still land, so nothing
  // stays held once it closes.
  world.observer<const z13::input::TouchDownEvent, TouchControls, const z13::TouchLayout, z13::input::InputState>(
      "TouchInputSystem::OnTouchDown")
      .event<z13::input::SystemInputEventType>()
      .without<z13::gameplay::Pause>()
      .without<z13::gameplay::FreeCursor>()
      .each(OnTouchDown);

  world.observer<const z13::input::TouchMoveEvent, TouchControls, const z13::TouchLayout,
                 const z13::input::InputConfig, z13::input::InputState>("TouchInputSystem::OnTouchMove")
      .event<z13::input::SystemInputEventType>()
      .each(OnTouchMove);

  world.observer<const z13::input::TouchUpEvent, TouchControls, z13::input::InputState>(
      "TouchInputSystem::OnTouchUp")
      .event<z13::input::SystemInputEventType>()
      .each(OnTouchUp);
}

}  // namespace

void TouchInputSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, [](flecs::world& w) {
    z13::flecs_tools::RegisterComponents<TouchControls, z13::TouchLayout>(w);
  });
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::gameplay::input
