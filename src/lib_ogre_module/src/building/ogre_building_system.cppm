// z13.ogre.building module interface unit.
// Внутренний модуль DLL lib_ogre_module (план миграции, раздел 4.5).

module;

#include <flecs.h>
// #include <Eigen/Dense>
// #include <Ogre.h>

// #include <lib_core/log.h>

export module z13.ogre.building;

export namespace z13::ogre {

class OgreBuildingSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::ogre