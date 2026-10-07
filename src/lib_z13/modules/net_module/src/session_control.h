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

#include <string>

#include <flecs.h>

#include <z13/components/net.h>

namespace z13::net {

void SetConnectionStatus(flecs::world world, ConnectionState state, std::string reason = {});

// Invalidates any NetSession reference: ServiceNetSession suspends defer, so the remove
// is immediate. Callers must not touch it afterwards.
void EndSession(flecs::world world);

// Ends the session of a client that can't go on, saying why.
void FailSession(flecs::world world, std::string reason);

bool IsJoined(flecs::world world);

}  // namespace z13::net
