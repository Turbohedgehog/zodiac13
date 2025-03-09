#include "system_base.h"

#include <typeinfo>

namespace the {
  std::string SystemBase::GetName() const {
    return typeid(*this).name();
  }
}  // namespace the