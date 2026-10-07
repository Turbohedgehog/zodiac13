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

#include "session_control.h"

#include <utility>

#include <lib_core/state/world_state.h>

#include "net_session.h"
#include "session_settings.h"

namespace z13::net {

void SetConnectionStatus(flecs::world world, ConnectionState state, std::string reason) {
  world.set<ConnectionStatus>({.state = state, .reason = std::move(reason)});
}

void EndSession(flecs::world world) {
  if (world.has<NetSession>()) {
    world.get_mut<NetSession>().Close();
    world.remove<NetSession>();
  }
  world.remove<ServerRole>();
  world.remove<ClientRole>();
  RestoreOwnSettings(world);
  // Queue systems keep running without a session and would apply the leftovers.
  z13::flecs_tools::ResetSessionScopedComponents(world);
}

void FailSession(flecs::world world, std::string reason) {
  SetConnectionStatus(world, ConnectionState::kFailed, std::move(reason));
  EndSession(world);
}

bool IsJoined(flecs::world world) {
  return world.has<ConnectionStatus>() && world.get<ConnectionStatus>().state == ConnectionState::kConnected;
}

}  // namespace z13::net
