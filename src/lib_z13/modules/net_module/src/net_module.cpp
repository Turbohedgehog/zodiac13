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

#include "net_module.h"

#include <flecs.h>

#include "action_hints.h"
#include "interest_system.h"
#include "lan_discovery_system.h"
#include "net_action_sender.h"
#include "net_session_system.h"
#include "remote_input.h"
#include "sector_system.h"

namespace z13::net {

NetModule::NetModule(flecs::world& world) {
  NetSessionSystem::Register(world);
  NetActionSender::Register(world);
  ActionHintSystem::Register(world);
  RemoteInput::Register(world);
  SectorSystem::Register(world);
  InterestSystem::Register(world);
  LanDiscoverySystem::Register(world);
}

}  // namespace z13::net
