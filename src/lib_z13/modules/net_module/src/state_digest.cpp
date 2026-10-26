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
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/rollback.h>
#include <lib_core/state/snapshot_hash.h>
#include <lib_core/state/state_markers.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/fnv_hash.h>
#include <lib_core/utils/log.h>
#include <lib_profile/profile.h>

#include <sectors/sector_index.h>
#include <z13/components/player.h>
#include <z13/components/player_action.h>

#include <net_module/clock_sync.h>
#include <z13_settings/building_tuning.h>

#include "interest_view.h"
#include "remote_input.h"
#include "sector_hash.h"
#include "server_session.h"
#include "wire_records.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;
namespace fbn = fbs::net;

uint64_t HashNames(const std::vector<std::string>& names) {
  uint64_t hash = kFnvOffsetBasis;
  const auto mix = [&hash](char c) { hash = FnvMix(hash, static_cast<uint8_t>(c)); };
  for (const std::string& name : names) {
    std::ranges::for_each(name, mix);
    mix('\0');
  }
  return hash;
}

bool PosesMatch(const fbn::PoseWire& a, const fbn::PoseWire& b) {
  return a.player_id() == b.player_id() && a.x() == b.x() && a.y() == b.y() && a.z() == b.z();
}

void RecordStateDigest(
    flecs::world world, uint64_t tick, const ft::WorldSnapshotHistory& snapshots, StateDigests& digests) {
  const std::optional<uint64_t> interval_ticks = ft::SnapshotIntervalTicks(world);
  const std::optional<uint64_t> retention_ticks = ft::SnapshotRetentionTicks(world);
  if (!interval_ticks || !retention_ticks || *interval_ticks == 0 || tick % *interval_ticks != 0) {
    return;
  }
  if (snapshots.history.Empty() || snapshots.history.Entries().back().tick != tick) {
    return;
  }
  digests.local.insert_or_assign(tick, ComputeStateDigest(world, snapshots.history.Entries().back()));
  std::erase_if(digests.local, [tick, retention = *retention_ticks](const auto& entry) {
    return entry.first + retention < tick;
  });
}

// An own command the server didn't have when it sent the digest may yet be moved past the
// digest's tick (CommandsRetimed), so until then the client's prediction may differ from it.
bool OwnCommandsSettled(const fbn::StateDigestT& remote, uint32_t local_id,
                        const z13::gameplay::PlayerActionLog& actions) {
  const auto through = std::ranges::find(remote.received_through, local_id, &fbn::PlayerTickWire::player_id);
  return std::ranges::none_of(actions.log.Entries(), [&](const z13::gameplay::PlayerActionRecord& record) {
    return record.player_id == local_id && record.tick <= remote.tick &&
        (through == remote.received_through.end() || record.tick > through->tick());
  });
}

std::string DescribePose(const std::optional<fbn::PoseWire>& pose) {
  return pose ? std::format("({:.3f}, {:.3f}, {:.3f})", pose->x(), pose->y(), pose->z()) : "absent";
}

std::optional<fbn::PoseWire> FindPose(const fbn::StateDigestT& digest, uint32_t player_id) {
  const auto pose = std::ranges::find(digest.positions, player_id, &fbn::PoseWire::player_id);
  return pose == digest.positions.end() ? std::nullopt : std::optional(*pose);
}

// What differs, for the log: the entity names, which players stand elsewhere, or neither.
std::string DescribeMismatch(const fbn::StateDigestT& local, const fbn::StateDigestT& remote) {
  std::string description = std::format("state digest mismatch at tick {}: {} entities (server {})", remote.tick,
                                        local.entity_count, remote.entity_count);
  if (local.name_hash != remote.name_hash) {
    description += ", entity names differ";
  }
  std::vector<uint32_t> players;
  for (const auto* digest : {&local, &remote}) {
    std::ranges::transform(digest->positions, std::back_inserter(players), &fbn::PoseWire::player_id);
  }
  std::ranges::sort(players);
  const auto [first, last] = std::ranges::unique(players);
  players.erase(first, last);
  for (const uint32_t player_id : players) {
    const auto mine = FindPose(local, player_id);
    const auto theirs = FindPose(remote, player_id);
    if (!mine || !theirs || !PosesMatch(*mine, *theirs)) {
      description += std::format(", player {} at {} (server {})", player_id, DescribePose(mine), DescribePose(theirs));
    }
  }
  if (local.name_hash == remote.name_hash && std::ranges::equal(local.positions, remote.positions, PosesMatch)) {
    description += ", other state differs";
  }
  return description;
}

std::optional<uint64_t> HashOfSector(const fbn::StateDigestT& digest, const sectors::SectorKey& sector) {
  const auto found = std::ranges::find_if(digest.sector_hashes, [&](const fbn::SectorHashWire& entry) {
    return entry.x() == sector.x() && entry.y() == sector.y() && entry.z() == sector.z();
  });
  return found == digest.sector_hashes.end() ? std::nullopt : std::optional(found->hash());
}

// A client with interest events holds part of the world: it checks the sectors it shows, not the rest.
bool InnerRingMatches(flecs::world world, uint32_t local_id, const fbn::StateDigestT& local,
                      const fbn::StateDigestT& remote) {
  const auto center = world.get<sectors::SectorIndex>().Locate({.kind = sectors::MemberKind::kPlayer, .id = local_id});
  if (!center) {
    return true;
  }
  const int radius = world.get<NetTuning>().interest_radius_sectors;
  for (int dx = -radius; dx <= radius; ++dx) {
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dz = -radius; dz <= radius; ++dz) {
        const sectors::SectorKey sector = *center + sectors::SectorKey(dx, dy, dz);
        if (HashOfSector(local, sector) != HashOfSector(remote, sector)) {
          return false;
        }
      }
    }
  }
  return true;
}

// What a client with interest events can compare: its view's sectors and players.
fbn::StateDigestT DigestOfView(const fbn::StateDigestT& digest, const InterestView& view) {
  fbn::StateDigestT part(digest);
  const auto outside = [&view](const auto& entry) { return !view.players.contains(entry.player_id()); };
  std::erase_if(part.positions, outside);
  std::erase_if(part.input_through, outside);
  std::erase_if(part.received_through, outside);
  std::erase_if(part.sector_hashes, [&view](const fbn::SectorHashWire& entry) {
    return !view.sectors.contains(sectors::SectorKey(entry.x(), entry.y(), entry.z()));
  });
  return part;
}

void SendDigest(flecs::world world, NetSession& session, const fbn::StateDigestT& digest) {
  if (!world.get<NetTuning>().interest_events) {
    Envelope envelope;
    envelope.body.Set(fbn::StateDigestT(digest));
    session.Broadcast(Channel::kReliable, envelope);
    return;
  }
  for (const auto& [connection, client] : session.PlayerConnections()) {
    const std::optional<InterestView> view = ViewOf(world, client);
    Envelope envelope;
    envelope.body.Set(view ? DigestOfView(digest, *view) : fbn::StateDigestT(digest));
    session.Send(connection, Channel::kReliable, envelope);
  }
}

}  // namespace

fbn::StateDigestT ComputeStateDigest(flecs::world world, const ft::TimestampedSnapshot& snapshot) {
  Z13_PROFILE_ZONE("ComputeStateDigest");
  std::vector<std::string> names;
  world.query_builder().with<ft::StateEntity>().build().each(
      [&names](flecs::entity e) { names.emplace_back(e.path().c_str()); });
  std::ranges::sort(names);

  fbn::StateDigestT digest;
  digest.tick = snapshot.tick;
  digest.state_hash = ft::HashSnapshot(snapshot.snapshot);
  if (world.has<sectors::SectorIndex>() && world.has<z13::BuildingTuning>()) {
    SectorHashes by_sector = HashBySector(
        world, snapshot.snapshot, world.get<sectors::SectorIndex>(), world.get<z13::BuildingTuning>().index_chunk_cells);
    digest.global_hash = by_sector.global;
    digest.sector_hashes = std::move(by_sector.by_sector);
  }
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
  return a.tick == b.tick && a.state_hash == b.state_hash;
}

void RegisterStateDigestSystems(flecs::world world) {
  // PostFrame after WorldSnapshotHistory's capture (lib_core registers it before any module), so
  // the digest hashes this tick's snapshot.
  world.system<StateDigests, const ft::SimulationClock, const ft::WorldSnapshotHistory>("StateDigest::Record")
      .kind(flecs::PostFrame)
      .with<NetSession>()
      .each([](flecs::iter& it, size_t, StateDigests& digests, const ft::SimulationClock& clock,
               const ft::WorldSnapshotHistory& snapshots) { RecordStateDigest(it.world(), clock.tick, snapshots, digests); });
}

// Every command for tick T reaches the server by T + max_late_ticks, and reaches clients
// ahead of this digest on the same ordered channel; T is final once every player is confirmed
// through it (FinalizeLateInput).
void SendSettledStateDigests(flecs::world world, NetSession& session, StateDigests& digests) {
  Z13_PROFILE_ZONE("SendSettledStateDigests");
  const uint64_t now = world.get<ft::SimulationClock>().tick;
  const std::optional<uint64_t> deferred_rollback_tick = ft::DeferredRollbackTick(world);
  const uint64_t max_late_ticks = world.get<NetTuning>().max_late_ticks;
  const std::optional<uint64_t> all_confirmed = AllConfirmedThrough(world.get<z13::gameplay::ConfirmedInputTicks>());
  for (const auto& [tick, digest] : digests.local) {
    // A command for `tick` is still accepted at exactly tick + max_late_ticks.
    if (tick + max_late_ticks >= now || tick > deferred_rollback_tick.value_or(tick) ||
        tick > all_confirmed.value_or(tick)) {
      break;
    }
    if (digests.last_sent_tick && tick <= *digests.last_sent_tick) {
      continue;
    }
    fbn::StateDigestT sent(digest);
    sent.input_through = ToWire(world.get<z13::gameplay::ConfirmedInputTicks>().by_player);
    sent.received_through = ToWire(world.get<ReceivedInputTicks>().by_player);
    SendDigest(world, session, sent);
    digests.last_sent_tick = tick;
  }
}

std::optional<DigestMismatch> CheckReceivedStateDigests(flecs::world world, StateDigests& digests) {
  const uint64_t now = world.get<ft::SimulationClock>().tick;
  const std::optional<uint64_t> deferred_rollback_tick = ft::DeferredRollbackTick(world);
  const std::optional<uint32_t> local_id = world.get<z13::gameplay::LocalPlayer>().id;
  const auto& actions = world.get<z13::gameplay::PlayerActionLog>();
  std::optional<DigestMismatch> mismatch;
  std::erase_if(digests.received, [&](const fbn::StateDigestT& remote) {
    // Strictly past: tick `now`'s PostFrame, which records its digest, hasn't run yet.
    if (remote.tick >= now || remote.tick > deferred_rollback_tick.value_or(remote.tick)) {
      return false;
    }
    const auto local = digests.local.find(remote.tick);
    if (local == digests.local.end() || !local_id || !OwnCommandsSettled(remote, *local_id, actions)) {
      return true;
    }
    ++digests.checked;
    const bool matches = world.get<NetTuning>().interest_events
        ? InnerRingMatches(world, *local_id, local->second, remote)
        : DigestsMatch(local->second, remote);
    if (!mismatch && !matches) {
      mismatch = DigestMismatch {.tick = remote.tick, .description = DescribeMismatch(local->second, remote)};
    } else if (!mismatch) {
      log_debug("NetSession(client): state digest at tick {} matches", remote.tick);
    }
    return true;
  });
  return mismatch;
}

}  // namespace z13::net
