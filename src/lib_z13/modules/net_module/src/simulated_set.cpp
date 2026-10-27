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

#include "simulated_set.h"

#include <algorithm>
#include <iterator>
#include <tuple>
#include <vector>

namespace z13::net {

std::unordered_set<uint32_t> ChooseSimulated(
    std::span<const SimulatedCandidate> candidates, const std::unordered_set<uint32_t>& current,
    const SimulatedBudget& budget) {
  std::vector<SimulatedCandidate> eligible;
  std::ranges::copy_if(candidates, std::back_inserter(eligible), [&](const SimulatedCandidate& candidate) {
    const double reach = budget.radius_m + (current.contains(candidate.player_id) ? budget.hysteresis_m : 0.);
    return candidate.distance_m <= reach;
  });
  // Ties by id, so that every pass ranks equal distances the same way.
  std::ranges::sort(eligible, [](const SimulatedCandidate& a, const SimulatedCandidate& b) {
    return std::tie(a.distance_m, a.player_id) < std::tie(b.distance_m, b.player_id);
  });
  std::unordered_set<uint32_t> chosen;
  for (size_t rank = 0; rank < eligible.size(); ++rank) {
    const uint32_t player_id = eligible[rank].player_id;
    if (rank < budget.max_players || (rank < budget.max_players + budget.slack && current.contains(player_id))) {
      chosen.insert(player_id);
    }
  }
  return chosen;
}

}  // namespace z13::net
