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
#include <memory>
#include <optional>
#include <vector>

#include <flecs.h>

#include <net_module/protocol.h>
#include <z13/components/held_sectors.h>

namespace z13::net {

// Each entry holds from its tick until the next one.
using BoundsByTick = std::map<uint64_t, std::optional<z13::station::SealedBounds>>;

// The server's station bounds by tick (docs/interest-design.md): its own on the server, those it sent on a client
// under interest, which cuts its rooms by them.
struct StationBoundsLog {
  using Singleton = void;
  using SessionScoped = void;
  BoundsByTick by_tick;
};

// Nothing before the first entry.
std::optional<z13::station::SealedBounds> BoundsAt(const BoundsByTick& log, uint64_t tick);
// `bounds` from `tick` on, in place of the later entries; false when the log holds them at `tick` already.
bool SetBounds(BoundsByTick& log, uint64_t tick, const std::optional<z13::station::SealedBounds>& bounds);
// Drops the entries before the one that holds at `tick`.
void SettleBounds(BoundsByTick& log, uint64_t tick);

// Server, every tick: logs StationBounds and sends a change to every client under interest.
void SendStationBounds(flecs::world world);
// The entries a CatchUp from `snapshot_tick` replays through.
std::vector<std::unique_ptr<fbs::net::StationBoundsT>> BoundsSince(flecs::world world, uint64_t snapshot_tick);
BoundsByTick BoundsFromWire(const std::vector<std::unique_ptr<fbs::net::StationBoundsT>>& wire);
void ReceiveStationBounds(flecs::world world, const fbs::net::StationBoundsT& message);

}  // namespace z13::net
