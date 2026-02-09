#pragma once

#include <lib_core/core_types.h>

namespace z13::gameplay::input {

class GameplayInputSystem {
 public:
  static void Register(flecs::world& world);
};

}  // z13::gameplay::input
