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

#include <lib_core/flecs_utils.h>

#include <lib_core/components.h>
#include <lib_core/core.h>

namespace z13 {

WorldNoDeferGuard::WorldNoDeferGuard(flecs::world world)
  : world_(world) {
  world_.defer_end();
}

WorldNoDeferGuard::~WorldNoDeferGuard() {
  world_.defer_begin();
}

ImmediateScope::ImmediateScope(flecs::world& world)
    : world_(world), needs_resume_(world.is_deferred() && !world.is_defer_suspended()) {
  if (needs_resume_) {
    world_.defer_suspend();
  }
}

ImmediateScope::~ImmediateScope() {
  if (needs_resume_) {
    world_.defer_resume();
  }
}

std::optional<std::reference_wrapper<const Config>> GetCoreConfig(flecs::world world) {
  if (!world.has<CoreComponent>()) {
    return std::nullopt;
  }
  const CoreComponent& core_component = world.get<CoreComponent>();
  if (!core_component.core) {
    return std::nullopt;
  }
  return std::cref(core_component.core->get().GetConfig());
}

}  // namespace z13
