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

#include <lib_core/world_state_requests.h>

#include <utility>

#include <lib_core/world_json_store.h>
#include <lib_core/world_state.h>

namespace z13::flecs_tools {

namespace {

// Singleton-only term, so $this is empty: use the iter/row overload.
void ProcessRequests(flecs::iter& it, size_t, PendingWorldState& pending) {
  // Moved out first: an operation may file further requests.
  auto operations = std::exchange(pending.operations, {});
  flecs::world world = it.world();
  for (const auto& operation : operations) {
    operation(world);
  }
}

void Enqueue(flecs::world& world, std::function<void(flecs::world&)> operation) {
  world.get_mut<PendingWorldState>().operations.push_back(std::move(operation));
}

}  // namespace

void RegisterWorldStateRequests(flecs::world& world) {
  RegisterComponent<PendingWorldState>(world);
  world.set<PendingWorldState>({});

  world.system<PendingWorldState>("WorldState::ProcessRequests")
      .kind(flecs::PreFrame)
      .immediate()
      .each(ProcessRequests);
}

void RequestSaveWorldState(flecs::world& world, SaveWorldStateCallback on_saved) {
  Enqueue(world, [on_saved = std::move(on_saved)](flecs::world& w) {
    if (on_saved) {
      on_saved(WorldJsonStore::Save(w));
    }
  });
}

void RequestLoadWorldState(flecs::world& world, std::string json, LoadWorldStateCallback on_loaded) {
  Enqueue(world, [json = std::move(json), on_loaded = std::move(on_loaded)](flecs::world& w) {
    const auto result = WorldJsonStore::Load(w, json);
    if (on_loaded) {
      on_loaded(result);
    }
  });
}

}  // namespace z13::flecs_tools
