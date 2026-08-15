// z13.core.flecs_utils module partition unit.
// Перенесён из include/lib_core/flecs_utils.h.

module;

#include <flecs.h>

export module z13.core.flecs_utils;

export namespace z13 {

class WorldNoDeferGuard {
 public:
  explicit WorldNoDeferGuard(flecs::world world);
  ~WorldNoDeferGuard();

 private:
  flecs::world world_;
};

}  // namespace z13