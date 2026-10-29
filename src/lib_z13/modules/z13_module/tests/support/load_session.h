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

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <net_module/in_memory_transport.h>

#include <z13_settings/settings_fwd.h>

#include "z13_test_world.h"

namespace z13::testing {

// What each client of a LoadSession does every tick.
struct Steering {
  bool walk = true;
  bool look = true;
  // Each client turns to a heading of its own before it walks, so they spread out over the station.
  bool fan_out = false;
};

// A server and N clients on one InMemoryNetwork, each client walking and turning its view every tick,
// for the measurements of docs/network-scaling-plan.md.
class LoadSession {
 public:
  // `tune` changes the settings of every world before it is built. Without `station_scene` the server hosts the ship
  // scene, where the walkers soon step off into the void and fall.
  explicit LoadSession(
      size_t clients, Steering steering = {}, const std::function<void(z13::Settings&)>& tune = [](z13::Settings&) {},
      std::optional<std::string_view> station_scene = std::nullopt);

  bool Connect(uint64_t max_ticks);
  // One frame of every world, the server first; only the server's frame is timed.
  void Tick();

  size_t ClientCount() const { return clients_.size(); }
  Z13TestWorld& Server() { return *server_; }
  Z13TestWorld& Client(size_t index) { return *clients_[index]; }
  z13::net::InMemoryNetwork& Network() { return *network_; }
  std::chrono::nanoseconds ServerTime() const { return server_time_; }

 private:
  bool AllConnected() const;
  void Steer(size_t index);

  std::shared_ptr<z13::net::InMemoryNetwork> network_ = std::make_shared<z13::net::InMemoryNetwork>();
  std::unique_ptr<Z13TestWorld> server_;
  std::vector<std::unique_ptr<Z13TestWorld>> clients_;
  Steering steering_;
  std::vector<bool> walking_;
  uint64_t tick_ {};
  std::chrono::nanoseconds server_time_ {};
};

}  // namespace z13::testing
