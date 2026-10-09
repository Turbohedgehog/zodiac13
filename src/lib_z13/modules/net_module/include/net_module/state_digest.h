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
#include <map>
#include <optional>
#include <vector>

#include <flecs.h>

#include <z13/components/player_action.h>

#include <net_module/protocol.h>

namespace z13::net {

// Absorbs cross-platform float drift; structure (entity names) must match exactly.
constexpr float kDigestPositionTolerance = 1e-3f;

// Desync detection (docs/client-server-plan.md, "Контроль рассинхрона"). `local` is
// recorded on snapshot ticks and re-recorded by a replay; `received` holds the server's
// digests until this client has reached and settled their tick.
struct StateDigests {
  using Singleton = void;
  using SessionScoped = void;
  std::map<uint64_t, fbs::net::StateDigestT> local;
  std::vector<fbs::net::StateDigestT> received;
  std::optional<uint64_t> last_sent_tick;
  // Set while awaiting a Resync: own records sent after the ResyncRequest, which the server
  // sequences only after building the Resync.
  std::optional<std::vector<z13::gameplay::PlayerActionRecord>> awaiting_resync;
  uint64_t checked {};
  uint64_t resyncs {};
};

fbs::net::StateDigestT ComputeStateDigest(flecs::world world, uint64_t tick);

bool DigestsMatch(const fbs::net::StateDigestT& a, const fbs::net::StateDigestT& b);

}  // namespace z13::net
