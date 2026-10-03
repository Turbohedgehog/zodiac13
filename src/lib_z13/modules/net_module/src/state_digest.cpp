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

#include "state_digest_system.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_state.h>

#include <z13/components/gameplay.h>

#include <net_module/clock_sync.h>

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;

// FNV-1a: std::hash may differ between the participants' standard libraries.
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t HashNames(const std::vector<std::string>& names) {
  uint64_t hash = kFnvOffsetBasis;
  const auto mix = [&hash](char c) { hash = (hash ^ static_cast<uint8_t>(c)) * kFnvPrime; };
  for (const std::string& name : names) {
    std::ranges::for_each(name, mix);
    mix('\0');
  }
  return hash;
}

bool PosesMatch(const fbn::PoseWire& a, const fbn::PoseWire& b) {
  return a.player_id() == b.player_id() && std::abs(a.x() - b.x()) <= kDigestPositionTolerance &&
      std::abs(a.y() - b.y()) <= kDigestPositionTolerance && std::abs(a.z() - b.z()) <= kDigestPositionTolerance;
}

void RecordStateDigest(flecs::world world, uint64_t tick, StateDigests& digests) {
  const std::optional<uint64_t> interval_ticks = ft::SnapshotIntervalTicks(world);
  const std::optional<uint64_t> retention_ticks = ft::SnapshotRetentionTicks(world);
  if (!interval_ticks || !retention_ticks || *interval_ticks == 0 || tick % *interval_ticks != 0) {
    return;
  }
  digests.local.insert_or_assign(tick, ComputeStateDigest(world, tick));
  std::erase_if(digests.local, [tick, retention = *retention_ticks](const auto& entry) {
    return entry.first + retention < tick;
  });
}

}  // namespace

fbn::StateDigestT ComputeStateDigest(flecs::world world, uint64_t tick) {
  std::vector<std::string> names;
  world.query_builder().with<ft::StateEntity>().build().each(
      [&names](flecs::entity e) { names.emplace_back(e.path().c_str()); });
  std::ranges::sort(names);

  fbn::StateDigestT digest;
  digest.tick = tick;
  digest.entity_count = static_cast<uint32_t>(names.size());
  digest.name_hash = HashNames(names);
  world.query_builder<const z13::gameplay::Player, const Eigen::Matrix4f>().build().each(
      [&digest](const z13::gameplay::Player& player, const Eigen::Matrix4f& transform) {
        digest.positions.emplace_back(player.id, transform(0, 3), transform(1, 3), transform(2, 3));
      });
  std::ranges::sort(digest.positions, {}, &fbn::PoseWire::player_id);
  return digest;
}

bool DigestsMatch(const fbn::StateDigestT& a, const fbn::StateDigestT& b) {
  return a.tick == b.tick && a.entity_count == b.entity_count && a.name_hash == b.name_hash &&
      std::ranges::equal(a.positions, b.positions, PosesMatch);
}

void RegisterStateDigestSystems(flecs::world world) {
  // PostFrame like WorldSnapshotHistory's capture, so a digest describes its snapshot.
  world.system<StateDigests, const ft::SimulationClock>("StateDigest::Record")
      .kind(flecs::PostFrame)
      .with<NetSession>()
      .each([](flecs::iter& it, size_t, StateDigests& digests, const ft::SimulationClock& clock) {
        RecordStateDigest(it.world(), clock.tick, digests);
      });
}

// Every command for tick T reaches the server by T + max_late_ticks, and reaches clients
// ahead of this digest on the same ordered channel.
NetSession::Result SendSettledStateDigests(flecs::world world, NetSession& session, StateDigests& digests) {
  const uint64_t now = world.get<ft::SimulationClock>().tick;
  const std::optional<uint64_t> deferred_rollback_tick = ft::DeferredRollbackTick(world);
  const uint64_t max_late_ticks = world.get<NetTuning>().max_late_ticks;
  for (const auto& [tick, digest] : digests.local) {
    // A command for `tick` is still accepted at exactly tick + max_late_ticks.
    if (tick + max_late_ticks >= now || tick > deferred_rollback_tick.value_or(tick)) {
      break;
    }
    if (digests.last_sent_tick && tick <= *digests.last_sent_tick) {
      continue;
    }
    Envelope envelope;
    envelope.body.Set(fbn::StateDigestT(digest));
    if (const auto sent = session.Broadcast(Channel::kReliable, envelope); !sent) {
      return sent;
    }
    digests.last_sent_tick = tick;
  }
  return {};
}

std::optional<std::string> CheckReceivedStateDigests(flecs::world world, StateDigests& digests) {
  const uint64_t now = world.get<ft::SimulationClock>().tick;
  const std::optional<uint64_t> deferred_rollback_tick = ft::DeferredRollbackTick(world);
  std::optional<std::string> mismatch;
  std::erase_if(digests.received, [&](const fbn::StateDigestT& remote) {
    // Strictly past: tick `now`'s PostFrame, which records its digest, hasn't run yet.
    if (remote.tick >= now || remote.tick > deferred_rollback_tick.value_or(remote.tick)) {
      return false;
    }
    const auto local = digests.local.find(remote.tick);
    if (local == digests.local.end()) {
      return true;
    }
    ++digests.checked;
    if (!mismatch && !DigestsMatch(local->second, remote)) {
      mismatch = std::format("state digest mismatch at tick {}: {} entities (server {})", remote.tick,
          local->second.entity_count, remote.entity_count);
    }
    return true;
  });
  return mismatch;
}

}  // namespace z13::net
