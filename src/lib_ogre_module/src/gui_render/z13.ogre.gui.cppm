// z13.ogre.gui module interface unit.
// Внутренний модуль DLL lib_ogre_module (план миграции, раздел 4.5).

module;

export module z13.ogre.gui;

import z13.core;
import z13.components;

export namespace z13::ogre::gui {

class GuiSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::ogre::gui