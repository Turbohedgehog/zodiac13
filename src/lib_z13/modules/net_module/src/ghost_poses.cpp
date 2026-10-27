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

#include "ghost_poses.h"

#include <algorithm>
#include <vector>

#include <lib_core/state/rollback.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/world/lifecycle.h>
#include <lib_profile/profile.h>

#include <z13/components/net.h>
#include <z13/components/player.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_settings/net_tuning.h>

#include "interest_events.h"
#include "net_session.h"
#include "pose_packet.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;

std::vector<PoseRecord> PosesOfPlayers(flecs::world world) {
  std::vector<PoseRecord> poses;
  world.query_builder<const z13::gameplay::Player, const Eigen::Matrix4f>().build().each(
      [&poses](const z13::gameplay::Player& player, const Eigen::Matrix4f& transform) {
        poses.push_back({.player_id = player.id,
                         .position = transform.block<3, 1>(0, 3),
                         .look = z13::gameplay::LookAnglesFromTransform(transform)});
      });
  std::ranges::sort(poses, {}, &PoseRecord::player_id);
  return poses;
}

bool IsDue(const z13::NetTuning& tuning, double distance_m, uint64_t tick) {
  return distance_m <= tuning.ghost_near_radius_m || tick / tuning.ghost_pose_interval_ticks % tuning.ghost_far_every == 0;
}

void SendGhostPoses(
    flecs::iter& it, size_t, NetSession& session, const ft::SimulationClock& clock, const InterestKnowledge& knowledge,
    const z13::NetTuning& tuning) {
  if (!tuning.ghosts || !tuning.interest_events || clock.tick % tuning.ghost_pose_interval_ticks != 0) {
    return;
  }
  Z13_PROFILE_ZONE("GhostPoseSystem::Send");
  const std::vector<PoseRecord> everyone = PosesOfPlayers(it.world());
  for (const auto& [connection, client] : session.PlayerConnections()) {
    const auto known = knowledge.by_client.find(client);
    const auto own = std::ranges::find(everyone, client, &PoseRecord::player_id);
    if (known == knowledge.by_client.end() || own == everyone.end()) {
      continue;
    }
    PosePacket packet {.tick = clock.tick};
    for (const PoseRecord& pose : everyone) {
      const double distance = (pose.position - own->position).norm();
      if (pose.player_id != client && !known->second.players.contains(pose.player_id) &&
          distance <= tuning.ghost_radius_m && IsDue(tuning, distance, clock.tick)) {
        packet.poses.push_back(pose);
      }
    }
    for (size_t from = 0; from < packet.poses.size(); from += kMaxPosesPerPacket) {
      const auto first = packet.poses.begin() + static_cast<std::ptrdiff_t>(from);
      const auto last = first + static_cast<std::ptrdiff_t>(std::min(kMaxPosesPerPacket, packet.poses.size() - from));
      session.SendBytes(connection, Channel::kPoses, EncodePoses({.tick = clock.tick, .poses = {first, last}}));
    }
  }
}

void RegisterSystems(flecs::world world) {
  world.system<NetSession, const ft::SimulationClock, const InterestKnowledge, const z13::NetTuning>(
          "GhostPoseSystem::Send")
      .kind(flecs::PostUpdate)
      .with<z13::net::ServerRole>()
      .without<ft::ReplayInProgress>()
      .each(SendGhostPoses);
}

}  // namespace

void GhostPoseSystem::Register(flecs::world& world) {
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
