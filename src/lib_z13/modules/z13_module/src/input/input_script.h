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

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Core>

#include <input_config_generated.h>

// An input script (--input-script): one step per line, `TICK ACTION`, the tick counted from
// the moment the local player is in the game. Actions: `KEY down`, `KEY up` (a Keycode name
// from input_config.fbs), `turn YAW PITCH` (degrees, like a mouse move) and `quit`. `#` starts
// a comment. Ticks must not decrease.
namespace z13::gameplay::input {

enum class ScriptAction { kKeyDown, kKeyUp, kTurn, kQuit };

struct ScriptStep {
  uint64_t tick {};
  ScriptAction action {};
  std::optional<z13::fbs::input::Keycode> key;
  Eigen::Vector2f turn_degrees = Eigen::Vector2f::Zero();
};

std::expected<std::vector<ScriptStep>, std::string> ParseInputScript(std::string_view text);

}  // namespace z13::gameplay::input
