// z13.module.building module interface unit.
// Внутренний модуль DLL z13_module (план миграции, раздел 4.5).

module;

export module z13.module.building;

import z13.core;
import z13.components;

export namespace z13::building {

class BuildingSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::building