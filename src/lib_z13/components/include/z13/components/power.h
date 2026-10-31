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
#include <vector>

namespace z13::utility {

// On a battery block: its energy in watt-ticks (1 W for one simulation tick). A new battery starts empty.
struct BatteryCharge {
  using State = void;
  int64_t watt_ticks {};

  bool operator==(const BatteryCharge&) const = default;
};

// What one power network did at the last balance; derived, never state.
struct PowerNetworkStatus {
  int64_t produced {};
  int64_t demanded {};
  int64_t served {};
  // Energy in its batteries, in watt-ticks.
  int64_t stored {};
};

struct PowerNetworkEntry {
  // The network's place in StationNetworks::networks.
  size_t network_index {};
  PowerNetworkStatus status;
};

// The last balance of each power network, for the overlay.
struct PowerStatus {
  using Singleton = void;
  std::vector<PowerNetworkEntry> networks;
};

}  // namespace z13::utility
