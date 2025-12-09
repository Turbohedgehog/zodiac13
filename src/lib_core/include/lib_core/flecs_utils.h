#pragma once

#include <flecs.h>

namespace z13 {

class WorldNoDeferGuard {
 public:
  explicit WorldNoDeferGuard(flecs::world world);
  ~WorldNoDeferGuard();

 private:
  flecs::world world_;
};

}  // namespace z13
