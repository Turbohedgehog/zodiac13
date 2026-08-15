// z13.module.bootstrap module interface unit.
// Внутренний модуль DLL z13_module (план миграции, раздел 4.5).

module;

#include <flecs.h>

export module z13.module.bootstrap;

export namespace z13::bootstrap {

class BootstrapSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::bootstrap