#pragma once

#include <lib_core/core_types.h>

namespace z13::gameplay {

class GameplaySystem {
 public:
  static void Register(flecs::world& world);
};

}  // z13::gameplay
