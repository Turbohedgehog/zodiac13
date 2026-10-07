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

#include "net_session.h"

namespace z13::net {

// May end the session.
void ServiceClientSession(flecs::world world, NetSession& session);

// A failed rollback after the join is one more way to diverge; during it, it's fatal
// (FinishPendingJoin).
void ResyncIfDiverged(flecs::world world, NetSession& session);

// Runs once the world is back in the present; returns whether the session is still open.
bool FinishPendingJoin(flecs::world world);

// Unreliable: behind reliable traffic it would measure the queue, not the network.
void SendPingIfDue(flecs::world world, NetSession& session);

}  // namespace z13::net
