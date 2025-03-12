#include "world.h"

#include "system_base.h"

namespace the {

World::World(WorldId id)
  : id_(id) {}

ECS& World::GetECS() {
  return ecs_;
}

const ECS& World::GetECS() const {
  return ecs_;
}

void World::Update(double delta_time) {}

void World::AddSystem(SystemPtr system) {
  systems_.emplace_back(std::move(system));
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

}  // namespace the
