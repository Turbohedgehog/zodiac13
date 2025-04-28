#include <lib_core/world.h>

#include <lib_core/system_base.h>

namespace z13 {

World::World(Core& core, WorldId id)
  : core_(core)
  , id_(id) {}

ECS& World::GetECS() {
  return ecs_;
}

const ECS& World::GetECS() const {
  return ecs_;
}

void World::Update(double delta_time) {
  for (auto& system : systems_) {
    system->Update(delta_time);
  }
}

void World::AddSystem(SystemPtr system) {
  systems_.emplace_back(system);
  system->OnAddedToWorld(shared_from_this());
}

bool World::IsPendindDestroy() const {
  return is_pending_destroy_;
}

void World::MarkToDestroy() {
  is_pending_destroy_ = true;
}

WorldId World::GetId() const {
  return id_;
}

bool World::Empty() const {
  return systems_.empty();
}

Core& World::GetCore() {
  return core_;
}

}  // namespace z13
