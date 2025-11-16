#pragma once

#include <lib_core/module_factory_base.h>


class Z13ModuleFactory : public z13::ModuleFactoryBase {
 public:
  void RegisterModules(flecs::world& world) override;
  const std::string& GetName() const override;
};
