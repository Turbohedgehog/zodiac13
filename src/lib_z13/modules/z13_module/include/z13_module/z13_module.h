#pragma once

#include <lib_core/module_base.h>

namespace z13 {

class Z13Module : public ModuleBase {
 public:
  const std::string& GetName() const override;
  void OnWorldCreated(WorldWeakPtr world) override;
};

} // namespace z13