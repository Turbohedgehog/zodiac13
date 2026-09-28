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

#include <net_module/state_digest.h>

#include "net_session.h"

namespace z13::net {

// Records StateDigests::local on snapshot ticks while a session is open.
void RegisterStateDigestSystems(flecs::world world);

// Server: broadcasts each recorded digest once no late command can change its tick.
NetSession::Result SendSettledStateDigests(flecs::world world, NetSession& session, StateDigests& digests);

// Client: compares the server's digests this client has reached. Returns whether any
// disagreed; digests with no local counterpart are dropped unchecked.
bool CheckReceivedStateDigests(flecs::world world, StateDigests& digests);

}  // namespace z13::net
