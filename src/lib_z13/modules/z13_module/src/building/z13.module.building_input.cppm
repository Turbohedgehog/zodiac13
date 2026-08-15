// z13.module.building_input module interface unit.
// Внутренний модуль DLL z13_module (план миграции, раздел 4.5).

module;

export module z13.module.building_input;

import z13.core;
import z13.components;

export namespace z13::building {

class BuildingInputSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::building