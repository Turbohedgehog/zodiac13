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

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_set>

namespace z13::net {

struct SimulatedCandidate {
  uint32_t player_id {};
  double distance_m {};
};

struct SimulatedBudget {
  size_t max_players {};
  size_t slack {};
  double radius_m {};
  double hysteresis_m {};
};

// The players a client simulates (docs/network-scaling-plan.md, stage 5b): the nearest ones within the radius.
// A player not simulated yet joins when it ranks within `max_players`; a simulated one stays while it ranks
// within `max_players + slack` and is within the radius plus the hysteresis, so that players trading places
// at the edge don't enter and leave every pass.
std::unordered_set<uint32_t> ChooseSimulated(
    std::span<const SimulatedCandidate> candidates, const std::unordered_set<uint32_t>& current,
    const SimulatedBudget& budget);

}  // namespace z13::net
