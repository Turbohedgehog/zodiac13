#pragma once

#include <map>
#include <string>
#include <vector>

#include "flecs.h"

#include "common_types.h"

namespace the {

class World {
 public:
  World(WorldId id);
  void Update(float delta_time);
  ECS& GetECS();
  const ECS& GetECS() const;
  void AddSystem(SystemPtr system);
  bool IsPendindDestrys() const;
  void MarkToDestroy();
  WorldId GetId() const;
  
 private:
  WorldId id_ = 0;
  ECS ecs_;
  std::vector<SystemPtr> systems_;
  bool is_pending_destroy_ = false;
};

}  // namespace the
