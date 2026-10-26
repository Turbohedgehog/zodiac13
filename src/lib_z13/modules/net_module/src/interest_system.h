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

#include <flecs.h>

#include "net_session.h"

namespace z13::net {

// Runs after the sector index is up to date.
struct InterestPhase {};

// What filtering the relayed commands by interest would have let through, over the relays so far.
struct InterestMetrics {
  using Singleton = void;
  using SessionScoped = void;
  uint64_t relays {};
  // Clients the commands went to, and those whose interest reached the sender.
  uint64_t recipients {};
  uint64_t needed {};
};

// Server only: gives every connected player an InterestMap entry around the sector it is in.
class InterestSystem {
 public:
  static void Register(flecs::world& world);
};

// Counts one relay of `sender`'s commands to the other clients. A command acts within the
// player's reach, so it is needed by the clients whose interest touches the sectors around the player.
void CountRelay(flecs::world world, const NetSession& session, uint32_t sender);

}  // namespace z13::net
