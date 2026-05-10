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
