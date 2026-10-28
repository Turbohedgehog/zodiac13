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

#include <flecs.h>

#include <net_module/protocol.h>

namespace z13::net {

// The client's side of the interest events (docs/interest-design.md); a rollback replays them.
void ApplyEntityEntered(flecs::world world, const fbs::net::EntityEnteredT& entered);
void ApplyEntityLeft(flecs::world world, const fbs::net::EntityLeftT& left);
void ApplySectorEntered(flecs::world world, const fbs::net::SectorEnteredT& entered);
void ApplySectorLeft(flecs::world world, const fbs::net::SectorLeftT& left);
// Not on the server, which made the edit.
void ApplyWorldEdit(flecs::world world, const fbs::net::WorldEditT& edit);

}  // namespace z13::net
