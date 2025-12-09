#include <lib_core/flecs_utils.h>

namespace z13 {

WorldNoDeferGuard::WorldNoDeferGuard(flecs::world world)
  : world_(world) {
  world_.defer_end();
}

WorldNoDeferGuard::~WorldNoDeferGuard() {
  world_.defer_begin();
}

}  // namespace z13
