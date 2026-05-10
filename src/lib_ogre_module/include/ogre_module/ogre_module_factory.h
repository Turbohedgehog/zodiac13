#pragma once

#include <memory>
#include <boost/config.hpp>
#include <boost/dll/alias.hpp>
#include <lib_core/module_factory_base.h>

extern "C" {

namespace z13::ogre {

class BOOST_SYMBOL_VISIBLE OgreModuleFactory : public z13::ModuleFactoryBase {
 public:
  static ModuleFactoryPtr CreateFactory();

  void RegisterModules(flecs::world& world) override;
  const std::string& GetName() const override;
};

}  // namespace z13::ogre

BOOST_DLL_ALIAS(
    z13::ogre::OgreModuleFactory::CreateFactory,
    create_module_factory
)

}  // extern "C"
