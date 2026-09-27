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

#pragma once

#include <functional>
#include <utility>

#include <flecs.h>

namespace z13 {

// World creation stages, run in this order by Core::CreateWorld.
enum class LifecycleStage {
  kRegisterComponents,
  kInitPhases,
  kInitSystems,
  kInitWorldData,
};

using LifecycleCallback = std::function<void(flecs::world&)>;

// Queues `callback` for `stage`; if that stage has already run, calls it right away.
// Callbacks of one stage run in registration order, with defer suspended.
void OnLifecycleStage(flecs::world& world, LifecycleStage stage, LifecycleCallback callback);

inline void OnRegisterComponents(flecs::world& world, LifecycleCallback callback) {
  OnLifecycleStage(world, LifecycleStage::kRegisterComponents, std::move(callback));
}
inline void OnInitPhases(flecs::world& world, LifecycleCallback callback) {
  OnLifecycleStage(world, LifecycleStage::kInitPhases, std::move(callback));
}
inline void OnInitSystems(flecs::world& world, LifecycleCallback callback) {
  OnLifecycleStage(world, LifecycleStage::kInitSystems, std::move(callback));
}
inline void OnInitWorldData(flecs::world& world, LifecycleCallback callback) {
  OnLifecycleStage(world, LifecycleStage::kInitWorldData, std::move(callback));
}

// Core::CreateWorld only: InitLifecycle before modules register, RunLifecycle after.
void InitLifecycle(flecs::world& world);
// Runs every stage in order, with LinearizePhases between phases and systems.
void RunLifecycle(flecs::world& world);

}  // namespace z13
