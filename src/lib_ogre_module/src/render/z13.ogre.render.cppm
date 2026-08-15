// z13.ogre.render module interface unit.
// Внутренний модуль DLL lib_ogre_module (план миграции, раздел 4.5).

module;

export module z13.ogre.render;

import z13.core;
import z13.components;

export namespace z13::ogre {

class EnvironmentRenderSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::ogre