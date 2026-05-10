#pragma once

#include <lib_core/core_types.h>

namespace z13::building {

class BuildingSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::building
