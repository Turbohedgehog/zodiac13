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

#include <lib_core/utils/system_times.h>

#include <algorithm>
#include <functional>

namespace z13 {

std::vector<SystemTime> SystemTimes(flecs::world world) {
  std::vector<SystemTime> times;
  world.query_builder().with(flecs::System).build().each([&](flecs::entity system) {
    if (const ecs_system_t* data = ecs_system_get(world, system)) {
      times.push_back({.path = std::string(system.path()), .ms = static_cast<double>(data->time_spent) * kMsPerSecond});
    }
  });
  return times;
}

std::vector<SystemTime> SystemTimesPerFrame(
    const std::vector<SystemTime>& before, const std::vector<SystemTime>& after, int frames) {
  std::vector<SystemTime> spent;
  for (const SystemTime& system : after) {
    const auto found = std::ranges::find(before, system.path, &SystemTime::path);
    const double earlier = found != before.end() ? found->ms : 0.0;
    spent.push_back({.path = system.path, .ms = (system.ms - earlier) / std::max(frames, 1)});
  }
  std::ranges::sort(spent, std::greater<>(), &SystemTime::ms);
  return spent;
}

}  // namespace z13