#pragma once

#include <memory>
#include <boost/config.hpp>
#include <boost/dll/alias.hpp>

#include <lib_core/module_factory_base.h>

extern "C" {

namespace z13::dll {

class BOOST_SYMBOL_VISIBLE TestDllModuleFactory : public z13::ModuleFactoryBase {
 public:
  static ModuleFactoryPtr CreateFactory();
  ~TestDllModuleFactory();
  void RegisterModules(flecs::world& world) override;
  const std::string& GetName() const override;
};

}  // namespace z13::dll

BOOST_DLL_ALIAS(
    z13::dll::TestDllModuleFactory::CreateFactory,
    create_module_factory
)

}  // extern "C"
