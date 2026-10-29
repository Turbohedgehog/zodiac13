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

#include "load_session.h"

#include <algorithm>
#include <string>
#include <utility>

#include <z13/components/gameplay.h>
#include <z13/components/input_events.h>
#include <z13/components/net.h>
#include <z13_settings/settings.h>

#include "building_test_helpers.h"
#include "test_network.h"

namespace z13::testing {

namespace {

using Keycode = z13::fbs::input::Keycode;

// Pixels of turn between neighbouring clients' headings; prime, so no number of clients lines two up.
constexpr int kFanOutStepX = 137;
constexpr int kLookStepX = 3;
constexpr int kLookJitterX = 5;
constexpr int kLookJitterY = 3;

}  // namespace

LoadSession::LoadSession(size_t clients, Steering steering, const std::function<void(z13::Settings&)>& tune,
                         std::optional<std::string_view> station_scene)
    : steering_(steering) {
  z13::Settings settings = TestSettings();
  tune(settings);
  walking_.assign(clients, false);
  std::vector server_args {std::string(kServerArg)};
  if (station_scene) {
    server_args.insert(server_args.end(), {std::string(kStationSceneArg), std::string(*station_scene)});
  }
  server_ = std::make_unique<Z13TestWorld>(std::move(server_args), network_, settings);
  for (size_t i = 0; i < clients; ++i) {
    clients_.push_back(std::make_unique<Z13TestWorld>(
        std::vector {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_, settings));
  }
}

bool LoadSession::AllConnected() const {
  return std::ranges::all_of(clients_, [](const std::unique_ptr<Z13TestWorld>& client) {
    return client->World().has<z13::gameplay::Gameplay>() &&
        client->World().get<z13::net::ConnectionStatus>().state == z13::net::ConnectionState::kConnected;
  });
}

bool LoadSession::Connect(uint64_t max_ticks) {
  for (uint64_t tick = 0; tick < max_ticks && !AllConnected(); ++tick) {
    Tick();
  }
  return AllConnected();
}

void LoadSession::Steer(size_t index) {
  Z13TestWorld& client = *clients_[index];
  if (steering_.walk && !walking_[index]) {
    if (steering_.fan_out) {
      z13::input::MouseMoveEvent turn;
      turn.delta = {.x = static_cast<int>(index) * kFanOutStepX, .y = 0};
      client.EmitInput(turn);
    }
    client.EmitInput(KeyDown(Keycode::KEY_W));
    walking_[index] = true;
  }
  if (!steering_.look) {
    return;
  }
  const uint64_t phase = tick_ + index;
  z13::input::MouseMoveEvent look;
  look.delta = {.x = kLookStepX + static_cast<int>(phase % kLookJitterX),
                .y = static_cast<int>(phase % kLookJitterY) - 1};
  client.EmitInput(look);
}

void LoadSession::Tick() {
  const auto start = std::chrono::steady_clock::now();
  z13::flecs_tools::TickWorld(server_->World(), kNetTestDeltaTime);
  server_time_ += std::chrono::steady_clock::now() - start;
  for (size_t i = 0; i < clients_.size(); ++i) {
    if (clients_[i]->World().has<z13::gameplay::Gameplay>()) {
      Steer(i);
    }
    z13::flecs_tools::TickWorld(clients_[i]->World(), kNetTestDeltaTime);
  }
  network_->Tick();
  ++tick_;
}

}  // namespace z13::testing
