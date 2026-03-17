#include <z13_module/z13_module_factory.h>

#include <flecs.h>

#include "z13_module.h"

namespace z13 {

ModuleFactoryPtr Z13ModuleFactory::CreateFactory() {
  return std::make_shared<Z13ModuleFactory>();
}

void Z13ModuleFactory::RegisterModules(flecs::world& world) {
  world.import<Z13Module>();
}

const std::string& Z13ModuleFactory::GetName() const {
  static std::string name = "Z13ModuleFactory";

  return name;
}

}  // namespace z13
