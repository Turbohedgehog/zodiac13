#include <lib_core/system_base.h>

#include <string>
#include <typeinfo>

namespace z13 {
  std::string SystemBase::GetName() const {
    static const std::string kName = typeid(*this).name();

    return kName;
    // return typeid(*this).name();
  }

  void SystemBase::OnAddedToWorld(WorldPtr world) {
    world_ = world;
  }

  WorldWeakPtr SystemBase::GetWorld() const {
    return world_;
  }
}  // namespace z13