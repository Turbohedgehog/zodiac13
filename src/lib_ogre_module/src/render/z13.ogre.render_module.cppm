// z13.ogre.render_module module interface unit.
// Внутренний модуль DLL lib_ogre_module (план миграции, раздел 4.5).

module;

#include <flecs.h>

export module z13.ogre.render_module;

export namespace z13::ogre {

class GameplayRenderModule {
 public:
  GameplayRenderModule(flecs::world& world);
};

}  // namespace z13::ogre