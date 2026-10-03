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

#include <lib_core/world/lifecycle.h>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include <lib_core/utils/flecs_utils.h>
#include <lib_core/world/phase_order.h>

namespace z13 {

constexpr size_t kLifecycleStageCount = static_cast<size_t>(LifecycleStage::kInitWorldData) + 1;

// Outside the anonymous namespace: every module binary must resolve the same component.
struct LifecycleCallbacks {
  std::array<std::vector<LifecycleCallback>, kLifecycleStageCount> by_stage;
  size_t completed_stages {};
};

namespace {

void RunStage(flecs::world& world, LifecycleStage stage) {
  const ImmediateScope immediate(world);
  const auto index = static_cast<size_t>(stage);
  // By index and re-fetched: a callback may queue more callbacks for this same stage.
  for (size_t i = 0; i < world.get<LifecycleCallbacks>().by_stage[index].size(); ++i) {
    const LifecycleCallback callback = world.get<LifecycleCallbacks>().by_stage[index][i];
    callback(world);
  }
  world.get_mut<LifecycleCallbacks>().completed_stages = index + 1;
}

}  // namespace

void OnLifecycleStage(flecs::world& world, LifecycleStage stage, LifecycleCallback callback) {
  const auto index = static_cast<size_t>(stage);
  if (index < world.get<LifecycleCallbacks>().completed_stages) {
    const ImmediateScope immediate(world);
    callback(world);
    return;
  }
  world.get_mut<LifecycleCallbacks>().by_stage[index].push_back(std::move(callback));
}

void InitLifecycle(flecs::world& world) {
  world.component<LifecycleCallbacks>().add(flecs::Singleton);
  world.set<LifecycleCallbacks>({});
}

std::expected<void, std::string> RunLifecycle(flecs::world& world) {
  RunStage(world, LifecycleStage::kRegisterComponents);
  RunStage(world, LifecycleStage::kInitPhases);
  if (auto linearized = LinearizePhases(world); !linearized) {
    return linearized;
  }
  RunStage(world, LifecycleStage::kInitSystems);
  RunStage(world, LifecycleStage::kInitWorldData);
  return {};
}

}  // namespace z13
