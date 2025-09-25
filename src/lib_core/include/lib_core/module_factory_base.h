#pragma once

#include "core_types.h"

namespace z13 {

class ModuleFactoryBase {
 public:
  virtual void RegisterModules(flecs::world& world) = 0;
  virtual const std::string& GetName() const = 0;
};

}  // namespace z13
