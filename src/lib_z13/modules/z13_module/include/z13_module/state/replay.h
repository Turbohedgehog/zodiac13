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

namespace z13::state {

// The action side of a rollback (lib_core's rollback.h drives the rest): while
// ReplayInProgress is set, feeds each tick's PlayerActionLog records into
// ActionListener.action_values the same way live input would, so the re-simulated ticks
// take the same path as the original ones. Doesn't know what any action_id means.
class Replay {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::state
