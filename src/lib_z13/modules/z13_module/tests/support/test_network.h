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

#include <cstdint>
#include <functional>
#include <vector>

#include <lib_core/rollback.h>

#include <net_module/in_memory_transport.h>

#include "z13_test_world.h"

namespace z13::testing {

// Advances a set of Z13TestWorlds sharing one InMemoryNetwork tick by tick -- each
// world's own progress() first, then one network.Tick() to deliver sends -- until
// `condition` holds or max_ticks is reached. Returns whether it held, so a test asserts
// on convergence instead of guessing a fixed tick count.
inline bool RunNetworkUntil(
    z13::net::InMemoryNetwork& network, std::vector<std::reference_wrapper<Z13TestWorld>> worlds, float delta_time,
    uint64_t max_ticks, const std::function<bool()>& condition) {
  for (uint64_t tick = 0; tick < max_ticks; ++tick) {
    for (Z13TestWorld& world : worlds) {
      z13::flecs_tools::TickWorld(world.World(), delta_time);
    }
    network.Tick();
    if (condition()) {
      return true;
    }
  }
  return condition();
}

}  // namespace z13::testing
