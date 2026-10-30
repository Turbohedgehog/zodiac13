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

#include "station_bounds_log.h"

#include <iterator>
#include <ranges>

#include <lib_core/time/simulation_clock.h>
#include <z13_settings/net_tuning.h>

#include "net_session.h"
#include "remote_input.h"

namespace z13::net {

namespace {

using z13::station::SealedBounds;

fbs::net::Vector3iWire ToWire(const Eigen::Vector3i& value) {
  return {value.x(), value.y(), value.z()};
}

Eigen::Vector3i FromWire(const fbs::net::Vector3iWire* value) {
  return value ? Eigen::Vector3i(value->x(), value->y(), value->z()) : Eigen::Vector3i::Zero();
}

std::unique_ptr<fbs::net::StationBoundsT> ToWire(uint64_t tick, const std::optional<SealedBounds>& bounds) {
  auto message = std::make_unique<fbs::net::StationBoundsT>();
  message->apply_tick = tick;
  if (bounds) {
    message->present = true;
    message->min = std::make_unique<fbs::net::Vector3iWire>(ToWire(bounds->min));
    message->extent = std::make_unique<fbs::net::Vector3iWire>(ToWire(bounds->extent));
  }
  return message;
}

std::optional<SealedBounds> FromWire(const fbs::net::StationBoundsT& message) {
  if (!message.present) {
    return std::nullopt;
  }
  return SealedBounds {.min = FromWire(message.min.get()), .extent = FromWire(message.extent.get())};
}

// The entry that holds at `tick`, or end().
BoundsByTick::const_iterator HoldingAt(const BoundsByTick& log, uint64_t tick) {
  const auto after = log.upper_bound(tick);
  return after == log.begin() ? log.end() : std::prev(after);
}

}  // namespace

std::optional<SealedBounds> BoundsAt(const BoundsByTick& log, uint64_t tick) {
  const auto holding = HoldingAt(log, tick);
  return holding == log.end() ? std::nullopt : holding->second;
}

bool SetBounds(BoundsByTick& log, uint64_t tick, const std::optional<SealedBounds>& bounds) {
  if (const auto holding = HoldingAt(log, tick); holding != log.end() && holding->second == bounds) {
    return false;
  }
  log.erase(log.lower_bound(tick), log.end());
  log.emplace(tick, bounds);
  return true;
}

void SettleBounds(BoundsByTick& log, uint64_t tick) {
  if (const auto holding = HoldingAt(log, tick); holding != log.end()) {
    log.erase(log.begin(), holding);
  }
}

void SendStationBounds(flecs::world world) {
  const auto* station = world.try_get<z13::station::StationBounds>();
  if (!station || !world.get<NetTuning>().interest_events) {
    return;
  }
  auto& log = world.get_mut<StationBoundsLog>().by_tick;
  if (const std::optional<uint64_t> oldest = OldestSnapshotTick(world)) {
    SettleBounds(log, *oldest);
  }
  const uint64_t tick = world.get<z13::flecs_tools::SimulationClock>().tick;
  if (!SetBounds(log, tick, station->sealed)) {
    return;
  }
  const auto message = ToWire(tick, station->sealed);
  auto& session = world.get_mut<NetSession>();
  for (const auto& [connection, client] : session.PlayerConnections()) {
    Envelope envelope;
    envelope.body.Set(fbs::net::StationBoundsT(*message));
    session.Send(connection, Channel::kReliable, envelope);
  }
}

std::vector<std::unique_ptr<fbs::net::StationBoundsT>> BoundsSince(flecs::world world, uint64_t snapshot_tick) {
  const BoundsByTick& log = world.get<StationBoundsLog>().by_tick;
  auto first = HoldingAt(log, snapshot_tick);
  if (first == log.end()) {
    first = log.begin();
  }
  std::vector<std::unique_ptr<fbs::net::StationBoundsT>> wire;
  std::ranges::transform(std::ranges::subrange(first, log.end()), std::back_inserter(wire),
                         [](const auto& entry) { return ToWire(entry.first, entry.second); });
  return wire;
}

BoundsByTick BoundsFromWire(const std::vector<std::unique_ptr<fbs::net::StationBoundsT>>& wire) {
  BoundsByTick log;
  for (const auto& message : wire) {
    if (message) {
      SetBounds(log, message->apply_tick, FromWire(*message));
    }
  }
  return log;
}

void ReceiveStationBounds(flecs::world world, const fbs::net::StationBoundsT& message) {
  SetBounds(world.get_mut<StationBoundsLog>().by_tick, message.apply_tick, FromWire(message));
}

}  // namespace z13::net
