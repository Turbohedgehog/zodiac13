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
#include <vector>

#include <input_config_generated.h>

namespace z13::input {

inline size_t KeyIndex(z13::fbs::input::Keycode key) {
  return static_cast<size_t>(static_cast<int>(key) - static_cast<int>(z13::fbs::input::Keycode::MIN));
}

// A key or mouse button going down (1) or up (0); a touch stick's axis anywhere between.
struct KeyTransition {
  z13::fbs::input::Keycode key {};
  float value {};
};

struct InputState {
  using Singleton = void;
  std::array<float, static_cast<size_t>(z13::fbs::input::Keycode::MAX) + 1> input_state = {};
  // Transitions not yet in input_state: applied one per key a tick, so a press and its release
  // within one frame both reach the simulation.
  std::vector<KeyTransition> pending;

  // This frame's accumulated mouse-look delta; folded into action_values and
  // reset every frame in CalculateInputValues.
  float mouse_yaw_delta_deg {};
  float mouse_pitch_delta_deg {};
};

struct InputListener {};

}  // namespace z13::input
