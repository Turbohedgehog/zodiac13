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

#include <lib_core/core_types.h>

namespace z13::net {

// Empty for now: Transport/protocol (this branch) don't touch flecs yet. NetSession,
// handshake and the systems that drive a Transport from a world land in a later
// branch (docs/client-server-plan.md stage 3+); this class exists so net_module
// already has the same plugin shape as bullet_module/z13_module.
class NetModule {
 public:
  explicit NetModule(flecs::world& world);
};

}  // namespace z13::net
