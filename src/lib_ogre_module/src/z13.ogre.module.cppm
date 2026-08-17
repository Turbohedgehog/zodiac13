// z13.ogre.module module interface unit.
// Внутренний модуль DLL lib_ogre_module (план миграции, раздел 4.5).

module;

export module z13.ogre.module;

import z13.core;
import z13.ogre.system;
import z13.ogre.building;

export namespace z13::ogre {

class OgreRender {
 public:
  OgreRender(flecs::world& world);
};

}  // namespace z13::ogre