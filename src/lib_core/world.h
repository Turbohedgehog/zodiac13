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
  void Update(double delta_time);
  ECS& GetECS();
  const ECS& GetECS() const;
  void AddSystem(SystemPtr system);
  bool IsPendindDestroy() const;
  void MarkToDestroy();
  WorldId GetId() const;
  bool Empty() const;
  
 private:
  WorldId id_ = 0;
  ECS ecs_;
  std::vector<SystemPtr> systems_;
  bool is_pending_destroy_ = false;
};

}  // namespace the
