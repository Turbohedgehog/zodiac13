#pragma once

#include <lib_core/module_factory_base.h>

namespace z13::ogre {

class OgreModuleFactory : public ModuleFactoryBase {
 public:
  void RegisterModules(flecs::world& world) override;
  const std::string& GetName() const override;
};

}  // namespace z13::ogre
