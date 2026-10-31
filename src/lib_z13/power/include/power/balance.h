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
#include <vector>

namespace z13::utility::power {

// A caller's id for a producer, consumer or battery: how it finds its result again.
using ItemId = uint64_t;

// Power is in watts; energy is in watt-ticks (1 W for one simulation tick), whole numbers all through.
struct Producer {
  ItemId id {};
  int64_t watts {};
};

struct Consumer {
  ItemId id {};
  int64_t watts {};
  // Lower is served first.
  uint8_t priority {};
};

struct Battery {
  ItemId id {};
  int64_t capacity {};
  int64_t charge {};
  // The most it takes in or gives out at once.
  int64_t rate {};
};

// One network's parts, each in the order the caller wants ties broken in (cell order, not entity ids).
struct Grid {
  std::vector<Producer> producers;
  std::vector<Consumer> consumers;
  std::vector<Battery> batteries;
};

struct ConsumerSupply {
  ItemId id {};
  bool satisfied {};
};

struct Balance {
  // Every consumer, in the order of Grid::consumers.
  std::vector<ConsumerSupply> supplies;
  // Each battery's charge after the step, in the order of Grid::batteries.
  std::vector<int64_t> charges;
  int64_t produced {};
  // What the supplied consumers asked for.
  int64_t served {};
  int64_t demanded {};
};

// Balances `grid` for `ticks` ticks. Consumers are served by priority, the order they came in within one, until the
// production plus what the batteries can give runs out; the rest, including smaller ones further down, get nothing.
// Surplus charges the batteries in order, a shortfall drains them. With zero ticks only the supplies are worked out.
Balance Solve(const Grid& grid, int64_t ticks);

}  // namespace z13::utility::power
