// z13.core.flecs_utils module implementation unit.
// Определяет WorldNoDeferGuardImpl, скрывающий flecs::world из
// экспортируемого интерфейса модуля z13.core.flecs_utils.

module;

#include <memory>

#include <flecs.h>

module z13.core.flecs_utils;

namespace z13 {

struct WorldNoDeferGuardImpl {
  flecs::world world_;
};

WorldNoDeferGuard::WorldNoDeferGuard(::flecs::world_t* world)
  : impl_(std::make_unique<WorldNoDeferGuardImpl>()) {
  impl_->world_ = flecs::world(world);
  impl_->world_.defer_end();
}

WorldNoDeferGuard::~WorldNoDeferGuard() {
  if (impl_) {
    impl_->world_.defer_begin();
  }
}

}  // namespace z13
