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
#include <string>
#include <vector>

#include <flecs.h>

namespace z13::gameplay {

// What the renderer decides a player by: drawn as a remote avatar unless it is the local listener.
struct LoggedPlayer {
  flecs::entity_t entity {};
  uint32_t id {};
  std::string name;
  bool listener {};
  bool has_transform {};
  bool operator==(const LoggedPlayer&) const = default;
};

struct PlayerRosterLog {
  using Singleton = void;
  std::vector<LoggedPlayer> players;
};

// Logs the player entities whenever they change, and their positions every few seconds.
class PlayerRosterLogSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::gameplay
