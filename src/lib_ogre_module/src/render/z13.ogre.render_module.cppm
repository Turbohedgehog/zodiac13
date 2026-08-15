// z13.ogre.render_module module interface unit.
// Внутренний модуль DLL lib_ogre_module (план миграции, раздел 4.5).

module;

export module z13.ogre.render_module;

import z13.core;
import z13.components;
import z13.ogre.render;
import z13.ogre.gui;

export namespace z13::ogre {

class GameplayRenderModule {
 public:
  GameplayRenderModule(flecs::world& world);
};

}  // namespace z13::ogre