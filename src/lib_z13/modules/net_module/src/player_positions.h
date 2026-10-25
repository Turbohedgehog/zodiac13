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
#include <optional>
#include <unordered_map>

#include <Eigen/Dense>
#include <flecs.h>

namespace z13::net {

struct PlayerPositions {
  using Singleton = void;
  using SessionScoped = void;
  std::optional<uint64_t> read_at_tick;
  std::unordered_map<uint32_t, Eigen::Vector3f> by_player;
};

// The positions as of the current tick: the world is read on a tick's first call, not once per relayed packet.
const PlayerPositions& CurrentPlayerPositions(flecs::world world);

}  // namespace z13::net
