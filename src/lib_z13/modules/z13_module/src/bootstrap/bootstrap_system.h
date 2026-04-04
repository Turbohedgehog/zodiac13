#pragma once

#include <lib_core/core_types.h>

namespace z13::bootstrap {

class BootstrapSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::bootstrap
