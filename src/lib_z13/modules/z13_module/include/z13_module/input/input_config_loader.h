#pragma once

#include <map>

namespace z13::input {

struct ActionMap;
struct InputConfig;
struct LookupForFlatbufActionEnumsEvent;

}  // namespace z13::input

namespace z13::gameplay::input {

class InputConfigLoader {
 public:
  static bool LoadConfig(z13::input::InputConfig& input_config);
  static bool SaveConfig(const z13::input::InputConfig& input_config);
  static void SetDefaults(z13::input::InputConfig& input_config);
  static void OnLookupForFlatbufActionEnums(
      const z13::input::LookupForFlatbufActionEnumsEvent& lookup_actions,
      z13::input::ActionMap& input_config);

 private:
  static void Clear(z13::input::InputConfig& input_config);
};

}  // namespace z13::gameplay
