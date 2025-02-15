#pragma once

#include <string>

namespace the {

class SystemBase {
 public:
  virtual std::string GetName() const;
};

}  // namespace the