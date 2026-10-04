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

#include <boost/container/flat_map.hpp>
#include <flecs.h>

#include <z13/components/input.h>

namespace z13::net {

using ActionValues = boost::container::flat_map<z13::input::ActionInfo::IdType, float>;

// Whether any of `values` is a held action Neutral prediction would release (not `absolute`).
bool HoldsReleasableAction(const ActionValues& values, const z13::input::ActionMap& action_map);

// Under RemoteInputPrediction::Neutral, records that `player_id`'s input up to `through_tick`
// has arrived, and replays the ticks that were predicted released while the player held an
// action. Returns whether the confirmed tick moved.
bool ConfirmInputThrough(flecs::world world, uint32_t player_id, uint64_t through_tick);

class RemoteInput {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::net
