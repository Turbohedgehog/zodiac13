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

#include <lib_core/state/world_serializer.h>

namespace z13::flecs_tools {

// Every name, tag, value and relationship in the snapshot, the same on every platform; a group
// counts by its fingerprint. What peers compare to find a desync (docs/net-core.md, Н6).
uint64_t HashSnapshot(const WorldSnapshot& snapshot);

}  // namespace z13::flecs_tools
