/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <lib_core/world_state.h>

#include <format>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/component_codec.h>
#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_snapshot_history.h>
#include <lib_core/world_state_requests.h>

namespace z13::flecs_tools {

bool IsStateSingleton(flecs::entity component) {
  return component.has<flecs::Component>() && component.has<StateComponent>() &&
         component.has(flecs::Singleton);
}

std::expected<void, std::string> ValidateStateComponents(flecs::world& world) {
  std::string errors;
  world.query_builder().with<StateComponent>().build().each([&](flecs::entity component) {
    const auto* info = component.try_get<flecs::Component>();
    if (info == nullptr || info->size == 0) {
      return;  // tags carry no value
    }
    if (const auto encodable = CheckEncodable(world, component); !encodable) {
      errors += std::format("{}'{}': {}", errors.empty() ? "" : "; ", component.path().c_str(), encodable.error());
    }
  });
  if (!errors.empty()) {
    return std::unexpected(std::format("State components can't be encoded: {}", errors));
  }
  return {};
}

// A singleton's value lives on its own component entity; add() default-constructs it.
// Immediate, since a deferred remove+add of one id may collapse into a no-op.
void ResetSessionScopedComponents(flecs::world& world) {
  const ImmediateScope immediate(world);
  std::vector<flecs::entity> components;
  world.query_builder().with<SessionScopedComponent>().build().each(
      [&components](flecs::entity component) { components.push_back(component); });
  for (const flecs::entity component : components) {
    component.remove(component);
    component.add(component);
  }
}

void RegisterStateMeta(flecs::world& world) {
  world.component<StateEntity>();
  world.component<StateComponent>();
  world.component<SessionScopedComponent>();
  RegisterStdStringMeta(world);
  RegisterEigenMeta(world);
  // Shared transform type; only state entities are captured, so this is safe module-wide.
  world.component<Eigen::Matrix4f>().add<StateComponent>();
  RegisterWorldStateRequests(world);
  RegisterSimulationClock(world);
  RegisterWorldSnapshotHistory(world);
  RegisterRollback(world);
}

}  // namespace z13::flecs_tools
