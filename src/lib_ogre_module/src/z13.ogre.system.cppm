// z13.ogre.system module interface unit.
// Внутренний модуль DLL lib_ogre_module (план миграции, раздел 4.5).

module;

export module z13.ogre.system;

export namespace z13::ogre {

class OgreSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::ogre