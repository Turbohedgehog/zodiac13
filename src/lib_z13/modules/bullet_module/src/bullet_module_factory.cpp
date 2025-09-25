#include <bullet_module/bullet_module_factory.h>

void BulletModuleFactory::RegisterModules(flecs::world& world) {

}

const std::string& BulletModuleFactory::GetName() const {
  static std::string name = "BulletModuleFactory";

  return name;
}