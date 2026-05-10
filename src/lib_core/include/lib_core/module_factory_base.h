#pragma once

#include <memory>
#include <string>
#include <boost/config.hpp>
#include <boost/dll/alias.hpp>

#include "core_types.h"

extern "C" {

namespace z13 {

// using ModuleFactoryPtr = std::shared_ptr<ModuleFactoryBase>;

class BOOST_SYMBOL_VISIBLE ModuleFactoryBase {
 public:
  virtual ~ModuleFactoryBase() = default;
  virtual void RegisterModules(flecs::world& world) = 0;
  virtual const std::string& GetName() const = 0;
};

}  // namespace z13

}  // extern "C"
