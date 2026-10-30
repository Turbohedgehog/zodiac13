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

#include "light_client.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <span>
#include <utility>

#include <flatbuffers/flatbuffers.h>

#include <z13/components/action_map.h>
#include <z13_module/input/input_config_loader.h>
#include <actions_generated.h>

#include "session_settings.h"
#include "wire_records.h"

namespace z13::net {

namespace {

namespace fbn = fbs::net;
namespace zgi = z13::gameplay::input;

std::expected<size_t, std::string> IndexOf(
    const std::vector<zgi::ActionDescriptor>& actions, z13::fbs::actions::Action action) {
  const auto found = std::ranges::find_if(actions, [action](const zgi::ActionDescriptor& described) {
    return described.enum_name == zgi::kActionsEnumName &&
        described.enum_value == static_cast<z13::input::ActionInfo::EnumValueType>(action);
  });
  if (found == actions.end()) {
    return std::unexpected(std::format("no action {}", z13::fbs::actions::EnumNameAction(action)));
  }
  return static_cast<size_t>(std::distance(actions.begin(), found));
}

// Verifies the envelope like DecodeMessage but unpacks nothing: most of what reaches a light client goes unread.
std::optional<fbn::MessageBody> PeekBody(std::span<const uint8_t> bytes) {
  flatbuffers::Verifier verifier(bytes.data(), bytes.size());
  if (!fbn::VerifyMessageEnvelopeBuffer(verifier)) {
    return std::nullopt;
  }
  return fbn::GetMessageEnvelope(bytes.data())->body_type();
}

}  // namespace

std::expected<LightClientSetup, std::string> MakeLightClientSetup(flecs::world world) {
  LightClientSetup setup {.actions = zgi::DescribeActions(world.get<z13::input::ActionMap>()),
                          .palette_hash = PaletteHash(world)};
  const auto forward = IndexOf(setup.actions, z13::fbs::actions::Action::MOVE_FORWARD);
  const auto yaw = IndexOf(setup.actions, z13::fbs::actions::Action::HORIZONTAL_LOOK);
  const auto pitch = IndexOf(setup.actions, z13::fbs::actions::Action::VERTICAL_LOOK);
  if (!forward || !yaw || !pitch) {
    return std::unexpected(!forward ? forward.error() : !yaw ? yaw.error() : pitch.error());
  }
  setup.sent = {.forward = *forward, .yaw = *yaw, .pitch = *pitch};
  return setup;
}

LightClient::LightClient(std::unique_ptr<Transport> transport, LightClientSetup setup)
    : setup_(std::move(setup)), recorded_(setup_.actions.size()) {
  session_.Open(std::move(transport));
}

void LightClient::Tick(const LightInput& input) {
  if (failure_) {
    return;
  }
  for (const TransportEvent& event : session_.Service()) {
    Receive(event);
  }
  if (!Joined() || failure_) {
    return;
  }
  if (!AdvanceClock()) {
    return;
  }
  Record(setup_.sent.forward, input.forward);
  Record(setup_.sent.yaw, input.yaw_deg);
  Record(setup_.sent.pitch, input.pitch_deg);
  SendDue();
}

void LightClient::Receive(const TransportEvent& event) {
  switch (event.kind) {
    case TransportEventKind::kConnected: {
      session_.AddConnection(event.connection);
      server_ = event.connection;
      fbn::ClientHelloT hello;
      hello.version = kProtocolVersion;
      if (setup_.palette_hash) {
        hello.palette_hash = *setup_.palette_hash;
      }
      for (const zgi::ActionDescriptor& action : setup_.actions) {
        auto described = std::make_unique<fbn::ActionDescT>();
        described->enum_name = action.enum_name;
        described->value_name = action.value_name;
        described->enum_value = action.enum_value;
        hello.actions.push_back(std::move(described));
      }
      Envelope envelope;
      envelope.body.Set(std::move(hello));
      session_.Send(event.connection, Channel::kReliable, envelope);
      return;
    }
    case TransportEventKind::kDisconnected:
      failure_ = "disconnected";
      return;
    case TransportEventKind::kReceived:
      break;
  }
  if (event.channel != Channel::kReliable && event.channel != Channel::kUnreliable) {
    return;
  }
  const std::span<const uint8_t> bytes = AsUint8(event.data);
  const std::optional<fbn::MessageBody> body = PeekBody(bytes);
  if (body != fbn::MessageBody::Welcome && body != fbn::MessageBody::Pong && body != fbn::MessageBody::Rejected) {
    return;
  }
  const auto decoded = DecodeMessage(bytes);
  if (!decoded) {
    failure_ = decoded.error();
    return;
  }
  VisitBody(decoded->body, Overloaded {
      [this](const fbn::WelcomeT& welcome) { Join(welcome); },
      [this](const fbn::RejectedT& rejected) { failure_ = rejected.reason; },
      [this](const fbn::PongT& pong) {
        if (Joined()) {
          ApplyPong(clock_, tuning_, pong.client_tick, pong.server_tick, tick_, adjust_);
        }
      },
      [](const auto&) {},
  });
}

void LightClient::Join(const fbn::WelcomeT& welcome) {
  if (!welcome.tuning || !welcome.catch_up || welcome.action_ids.size() != setup_.actions.size()) {
    failure_ = "unusable Welcome";
    return;
  }
  tuning_ = NetTuning(*welcome.tuning);
  ticks_per_second_ = static_cast<uint64_t>(std::llround(welcome.fps));
  action_ids_ = welcome.action_ids;
  player_id_ = welcome.player_id;
  // A full client replays to the server's tick and the Pongs then bring it its lead; this one has nothing to replay.
  tick_ = welcome.catch_up->server_tick + tuning_.clock_lead_ticks;
}

bool LightClient::AdvanceClock() {
  // Like ClockAdjustRequest on a full client: extra ticks are jumped, a negative adjustment skips frames.
  const bool skipped = adjust_.ticks < 0;
  if (skipped) {
    ++adjust_.ticks;
    --adjust_.applied;
  } else {
    tick_ += 1 + adjust_.ticks;
    adjust_.applied += adjust_.ticks;
    adjust_.ticks = 0;
  }
  adjust_.ticks += TakeClockAdjustment(clock_, tuning_, tick_);
  return !skipped;
}

void LightClient::Record(size_t action, float value) {
  const float canonical = z13::gameplay::CanonicalActionValue(value);
  if (recorded_[action] == canonical) {
    return;
  }
  recorded_[action] = canonical;
  outgoing_.push_back({.tick = tick_, .player_id = *player_id_, .action_id = action_ids_[action], .value = canonical});
}

void LightClient::SendDue() {
  if (ticks_per_second_ > 0 && tick_ % ticks_per_second_ == 0) {
    fbn::PingT ping;
    ping.client_tick = tick_;
    Envelope envelope;
    envelope.body.Set(std::move(ping));
    session_.Send(*server_, Channel::kUnreliable, envelope);
    clock_.ping_sent_tick = tick_;
    clock_.applied_at_ping = adjust_.applied;
  }
  if (tick_ % tuning_.send_interval_ticks != 0) {
    return;
  }
  auto run = ToCommandRun(outgoing_, tick_);
  outgoing_.clear();
  if (!run) {
    failure_ = run.error();
    return;
  }
  fbn::CommandBatchT batch;
  batch.base_tick = run->base_tick;
  batch.commands = std::move(run->commands);
  batch.through_tick = tick_;
  Envelope envelope;
  envelope.body.Set(std::move(batch));
  session_.Send(*server_, Channel::kReliable, envelope);
}

}  // namespace z13::net
