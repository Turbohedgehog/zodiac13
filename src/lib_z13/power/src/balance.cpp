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


#include <power/balance.h>

#include <algorithm>
#include <numeric>

namespace z13::utility::power {

namespace {

// What a battery can give per tick over a step of `ticks`.
int64_t Discharge(const Battery& battery, int64_t ticks) {
  return std::min(battery.rate, battery.charge / std::max<int64_t>(ticks, 1));
}

int64_t Intake(const Battery& battery, int64_t ticks) {
  return std::min(battery.rate, (battery.capacity - battery.charge) / std::max<int64_t>(ticks, 1));
}

}  // namespace

Balance Solve(const Grid& grid, int64_t ticks) {
  Balance balance;
  balance.produced = std::accumulate(grid.producers.begin(), grid.producers.end(), int64_t {},
                                     [](int64_t sum, const Producer& producer) { return sum + producer.watts; });
  const int64_t available = std::accumulate(
      grid.batteries.begin(), grid.batteries.end(), balance.produced,
      [ticks](int64_t sum, const Battery& battery) { return sum + Discharge(battery, ticks); });

  std::vector<size_t> order(grid.consumers.size());
  std::iota(order.begin(), order.end(), size_t {});
  std::ranges::stable_sort(order, {}, [&grid](size_t i) { return grid.consumers[i].priority; });

  balance.supplies.resize(grid.consumers.size());
  bool exhausted = false;
  for (const size_t i : order) {
    const Consumer& consumer = grid.consumers[i];
    balance.demanded += consumer.watts;
    exhausted = exhausted || balance.served + consumer.watts > available;
    balance.supplies[i] = {.id = consumer.id, .satisfied = !exhausted};
    if (!exhausted) {
      balance.served += consumer.watts;
    }
  }

  balance.charges.reserve(grid.batteries.size());
  int64_t shortfall = std::max<int64_t>(balance.served - balance.produced, 0);
  int64_t surplus = std::max<int64_t>(balance.produced - balance.served, 0);
  for (const Battery& battery : grid.batteries) {
    const int64_t drawn = std::min(shortfall, Discharge(battery, ticks));
    const int64_t given = std::min(surplus, Intake(battery, ticks));
    shortfall -= drawn;
    surplus -= given;
    balance.charges.push_back(battery.charge + (given - drawn) * ticks);
  }
  return balance;
}

}  // namespace z13::utility::power
