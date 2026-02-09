#pragma once

// #include <z13_module/components/input.h>
#include <flecs.h>

namespace z13::input {

struct InputConfig;

}  // namespace z13::input

namespace z13::gameplay::input {
  
class InputConverter {
 public:
  static void EmitInputEvent(flecs::world world, const z13::input::InputConfig& input_config);
};

}  // namespace z13::gameplay
