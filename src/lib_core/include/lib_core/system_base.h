#pragma once

#include <lib_core/common_types.h>

#include <string>

namespace z13 {

class SystemBase {
 public:
  virtual std::string GetName() const;
  virtual void OnAddedToWorld(WorldPtr world);
  virtual void Update(double delta_time) {}
  WorldWeakPtr GetWorld() const;

 private:
  WorldWeakPtr world_;
};

}  // namespace z13