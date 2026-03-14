#pragma once

#include <lib_core/core_types.h>

namespace z13::dll {

class TestDllSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::dll
