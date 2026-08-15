// z13.module.gameplay module interface unit.
// Внутренний модуль DLL z13_module (план миграции, раздел 4.5).

module;

export module z13.module.gameplay;

import z13.core;
import z13.components;

export namespace z13::gameplay {

class GameplaySystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::gameplay