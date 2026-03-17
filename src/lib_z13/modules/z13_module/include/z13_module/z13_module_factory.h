#pragma once

#include <memory>
#include <boost/config.hpp>
#include <boost/dll/alias.hpp>
#include <lib_core/module_factory_base.h>

namespace z13 {

class BOOST_SYMBOL_VISIBLE Z13ModuleFactory : public z13::ModuleFactoryBase {
 public:
  static ModuleFactoryPtr CreateFactory();

  void RegisterModules(flecs::world& world) override;
  const std::string& GetName() const override;
};

}  // namespace z13

BOOST_DLL_ALIAS(
    z13::Z13ModuleFactory::CreateFactory,
    create_module_factory
)