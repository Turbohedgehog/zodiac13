#include "modules/z13_module/z13_module.h"

namespace z13 {

const std::string& Z13Module::GetName() const {
  static const std::string kName = "Z13Module";
  
  return kName;
}

void Z13Module::OnWorldCreated(the::WorldWeakPtr world) {
  the::ModuleBase::OnWorldCreated(world);
}

}  // namespace z13
