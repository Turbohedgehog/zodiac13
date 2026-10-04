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

#include <flecs.h>

namespace z13::net {

// Under RemoteInputPrediction::Neutral, records that `player_id`'s input up to `through_tick`
// has arrived, and replays the ticks that were predicted released while the player held an
// action. Returns whether the confirmed tick moved.
bool ConfirmInputThrough(flecs::world world, uint32_t player_id, uint64_t through_tick);

class RemoteInput {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::net
