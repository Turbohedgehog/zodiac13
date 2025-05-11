#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>
#include <tuple>

#include "flecs.h"

#include <lib_core/common_types.h>
#include <lib_core/component_view.h>

namespace z13 {

class World : public std::enable_shared_from_this<World> {
 public:
  World(Core& core, WorldId id);
  void Update(double delta_time);
  ECS& GetECS();
  const ECS& GetECS() const;
  void AddSystem(SystemPtr system);
  bool IsPendindDestroy() const;
  void MarkToDestroy();
  WorldId GetId() const;
  bool Empty() const;
  Core& GetCore();

  template <typename T, typename... Ts>
  std::weak_ptr<T> CreateSystem(Ts&&... args) {
    auto system = std::make_shared<T>(std::forward<Ts>(args)...);
    AddSystem(system);

    return system;
  }

  template <typename... Ts>
  EntityView CreateEntity() {
    auto entity = ecs_.entity();
    (ecs_.add<Ts>(), ...);

    return EntityView(entity);
  }

  template <typename... Ts>
  EntityView CreateEntity(Ts&&... args) {
    auto entity = ecs_.entity();
    (ecs_.set<Ts>(std::forward<Ts>(args)), ...);

    return EntityView(entity);
  }
  
 private:
  Core& core_;
  WorldId id_ = 0;
  ECS ecs_;
  std::vector<SystemPtr> systems_;
  bool is_pending_destroy_ = false;
};

}  // namespace z13
