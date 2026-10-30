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

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <flecs.h>

#include <lib_core/state/rollback.h>
#include <z13/components/player_action.h>
#include <z13_module/input/action_negotiation.h>
#include <z13_settings/net_tuning.h>

#include <net_module/clock_sync.h>
#include <net_module/protocol.h>
#include <net_module/transport.h>

#include "net_session.h"

namespace z13::net {

// Indices into LightClientSetup::actions of what a light client sends.
struct LightActions {
  size_t forward {};
  size_t yaw {};
  size_t pitch {};
};

// What a ClientHello needs, taken from a world that has the game's actions and palette.
struct LightClientSetup {
  std::vector<z13::gameplay::input::ActionDescriptor> actions;
  LightActions sent;
  std::optional<uint64_t> palette_hash;
};

std::expected<LightClientSetup, std::string> MakeLightClientSetup(flecs::world world);

struct LightInput {
  float forward {};
  float yaw_deg {};
  float pitch_deg {};
};

// A client without a world for load measurements: it joins, keeps its clock and sends its input like a player, but
// simulates nothing, so it reads only the Welcome, Pongs and a Rejected.
class LightClient {
 public:
  LightClient(std::unique_ptr<Transport> transport, LightClientSetup setup);

  // One frame: what arrived, then `input` recorded at this client's tick and the batch sent if due.
  void Tick(const LightInput& input);

  bool Joined() const { return player_id_.has_value(); }
  const std::optional<uint32_t>& PlayerId() const { return player_id_; }
  const std::optional<std::string>& Failure() const { return failure_; }
  uint64_t CurrentTick() const { return tick_; }
  const SentTraffic& Sent() const { return session_.Sent(); }

 private:
  void Receive(const TransportEvent& event);
  void Join(const fbs::net::WelcomeT& welcome);
  // Whether this frame advanced the tick.
  bool AdvanceClock();
  void Record(size_t action, float value);
  void SendDue();

  NetSession session_;
  LightClientSetup setup_;
  std::optional<ConnectionId> server_;
  std::optional<uint32_t> player_id_;
  std::optional<std::string> failure_;
  NetTuning tuning_;
  uint64_t ticks_per_second_ {};
  // The server's id of each LightClientSetup::actions entry.
  std::vector<uint32_t> action_ids_;
  uint64_t tick_ {};
  ClockSync clock_;
  z13::flecs_tools::ClockAdjustRequest adjust_;
  // Last value recorded per LightClientSetup::actions entry, so only changes become commands.
  std::vector<std::optional<float>> recorded_;
  std::vector<z13::gameplay::PlayerActionRecord> outgoing_;
};

}  // namespace z13::net
