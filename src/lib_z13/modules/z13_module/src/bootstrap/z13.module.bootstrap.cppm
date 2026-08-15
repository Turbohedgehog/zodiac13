// z13.module.bootstrap module interface unit.
// Внутренний модуль DLL z13_module (план миграции, раздел 4.5).

module;

export module z13.module.bootstrap;

import z13.core;
import z13.components;

export namespace z13::bootstrap {

class BootstrapSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::bootstrap