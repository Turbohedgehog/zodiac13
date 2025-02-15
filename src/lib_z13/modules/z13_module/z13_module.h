#pragma once

#include "module_base.h"

namespace z13 {

class Z13Module : public the::ModuleBase {
 public:
  const std::string& GetName() const override;
  void OnWorldCreated(the::WorldWeakPtr world) override;
  
};

} // namespace z13