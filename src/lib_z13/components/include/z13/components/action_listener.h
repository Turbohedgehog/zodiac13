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

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <boost/container/flat_map.hpp>

#include <z13/components/action_id.h>

namespace z13::input {

struct CurrentActionListenerTag {};

struct ActionValueHolder {
  static constexpr float kInputValueEps = 0.001f;
  static constexpr float kSwithValue = 1.f;

  bool IsSwitchedOn() const {
    return current_value - prev_value >= kSwithValue - kInputValueEps;
  }

  bool IsSwitchedOff() const {
    return current_value - prev_value <= kInputValueEps - kSwithValue;
  }

  void IterateToNextState() {
    prev_value = current_value;
    current_value = {};
  }

  ActionValueHolder& operator+= (float value) {
    current_value += value;
    return *this;
  }

  ActionValueHolder& operator-= (float value) {
    current_value -= value;
    return *this;
  }

  float& operator* () {
    return current_value;
  }

  const float& operator* () const {
    return current_value;
  }

  bool HasBeenChanged() const {
    return std::abs(current_value - prev_value) >= kInputValueEps;
  }

  float current_value {};
  float prev_value {};
};

struct ActionListener {
  // action_values is filled by the clear phase at the end of the frame, so a listener
  // added earlier in one has nothing in it yet -- readers must tolerate that.
  std::optional<ActionValueHolder> Value(ActionId action_id) const {
    const auto value = action_values.find(action_id);
    return value == action_values.end() ? std::nullopt : std::optional(value->second);
  }

  std::vector<std::string> action_group_priority;
  boost::container::flat_map<ActionId, ActionValueHolder> action_values;
};

}  // namespace z13::input
