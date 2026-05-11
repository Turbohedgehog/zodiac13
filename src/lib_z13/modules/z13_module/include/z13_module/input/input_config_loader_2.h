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

#include <optional>

#include <z13/components/input.h>

// namespace z13::input {

// struct ActionMap;
// struct InputConfig;
// struct FlatbufferBinarySchema;

// }  // namespace z13::input

namespace z13::gameplay::input {

class InputConfigLoader2 {
 public:
  static bool LoadConfig(z13::input::InputConfig& input_config, const z13::input::ActionMap& action_map);
  static bool SaveConfig(const z13::input::InputConfig& input_config, const z13::input::ActionMap& action_map);
  static void SetDefaults(z13::input::InputConfig& input_config, const z13::input::ActionMap& action_map);
  static void Clear(z13::input::InputConfig& input_config);
  static void AppendFlatbufActionsFromBinarySchema(
      const z13::input::FlatbufferBinarySchema& binary_schema,
      z13::input::ActionMap& action_map);
  static std::optional<z13::input::ActionInfo::IdType> FindActionId(
    const z13::input::ActionMap::ActionMapContainer& action_map_container,
    std::string_view action_enum_name,
    z13::input::ActionInfo::EnumValueType action_enum_value
  );

  template <typename T>
  static std::optional<z13::input::ActionInfo::IdType> FindActionId(
    const z13::input::ActionMap::ActionMapContainer& action_map_container,
    std::string_view action_enum_name,
    T action_enum_value
  ) {
    return FindActionId(
        action_map_container,
        action_enum_name,
        static_cast<z13::input::ActionInfo::EnumValueType>(action_enum_value));
  }
};

}  // namespace z13::gameplay
