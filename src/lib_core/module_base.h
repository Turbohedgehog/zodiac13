#pragma once

#include <string>

#include "common_types.h"

namespace the {

class ModuleBase {
 public:
  virtual const std::string& GetName() const = 0;
  virtual void Init(const Core& core) {}
  virtual void OnWorldCreated(WorldWeakPtr world) {}
};

} // namespace the
