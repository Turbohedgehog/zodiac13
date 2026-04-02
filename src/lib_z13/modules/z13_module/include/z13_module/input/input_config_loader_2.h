#pragma once

namespace z13::input {

struct ActionMap;
struct InputConfig;
struct LookupForFlatbufActionEnumsEvent;

}  // namespace z13::input

namespace z13::gameplay::input {

class InputConfigLoader2 {
 public:
  static bool LoadConfig(z13::input::InputConfig& input_config, const z13::input::ActionMap& action_map);
  static bool SaveConfig(const z13::input::InputConfig& input_config, const z13::input::ActionMap& action_map);
  static void SetDefaults(z13::input::InputConfig& input_config, const z13::input::ActionMap& action_map);
  static void Clear(z13::input::InputConfig& input_config);
};

}  // namespace z13::gameplay
