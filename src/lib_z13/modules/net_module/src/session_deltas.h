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
#include <expected>
#include <string>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/utils/status.h>

#include <net_module/protocol.h>
#include <net_module/transport.h>

#include "interest_view.h"
#include "net_session.h"

namespace z13::net {

using SessionDelta = std::variant<
    fbs::net::PlayerJoinedT, fbs::net::PlayerLeftT, fbs::net::EntityEnteredT, fbs::net::EntityLeftT,
    fbs::net::SectorEnteredT, fbs::net::SectorLeftT>;

struct ScheduledSessionDelta {
  uint64_t apply_tick {};
  SessionDelta delta;
  uint32_t player_id {};  // the player the delta is about (not set on a client)
  // Server-side: reserved for this join, so later joins spawn elsewhere.
  std::optional<Eigen::Vector3f> spawn_position;
};

// `history` lets a rollback replay applied deltas and a joiner catch up on them.
struct ScheduledSessionDeltas {
  using Singleton = void;
  using SessionScoped = void;
  std::vector<ScheduledSessionDelta> pending;
  std::vector<ScheduledSessionDelta> history;
};

// The delta is sent to the clients that have `player_id` (SendToKnowing).
void ScheduleSessionDelta(
    NetSession& session, flecs::world world, uint64_t apply_tick, SessionDelta delta, uint32_t player_id,
    std::optional<Eigen::Vector3f> spawn_position = std::nullopt);
// A join worked out ahead: where the player will stand, so the Welcome can be made for that place.
struct PreparedJoin {
  fbs::net::PlayerJoinedT joined;
  Eigen::Vector3f spawn_position = Eigen::Vector3f::Zero();
};

std::expected<PreparedJoin, std::string> PrepareJoin(flecs::world world, uint32_t player_id);
void ScheduleJoin(NetSession& session, flecs::world world, uint32_t player_id, PreparedJoin join);
// Deltas apply before same-tick commands, so the leave waits past the player's last one.
uint64_t LeaveApplyTick(flecs::world world, uint32_t player_id);
// With a view, only the deltas about players in it.
void SendSessionDeltas(NetSession& session, ConnectionId connection, std::span<const ScheduledSessionDelta> deltas,
                       const std::optional<InterestView>& view = std::nullopt);

// Every tick, replay included: a replayed tick re-applies its history.
void ApplyDueSessionDeltas(flecs::world world);

}  // namespace z13::net
