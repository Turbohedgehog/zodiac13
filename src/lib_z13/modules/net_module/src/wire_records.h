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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <lib_core/state/world_serializer.h>
#include <z13/components/input.h>
#include <z13/components/player_action.h>

#include <net_module/protocol.h>

namespace z13::net {

std::vector<fbs::net::ActionRecordWire> ToWire(const std::vector<z13::gameplay::PlayerActionRecord>& records);
std::vector<z13::gameplay::PlayerActionRecord> FromWire(const std::vector<fbs::net::ActionRecordWire>& wire);
std::unique_ptr<fbs::state::WorldSnapshotT> ToWire(const z13::flecs_tools::WorldSnapshot& snapshot);

z13::gameplay::PlayerActionRecord FromWire(
    const fbs::net::CommandWire& command, uint64_t base_tick, uint32_t player_id);

bool IsKnownActionId(const z13::input::ActionMap& action_map, uint16_t action_id);

// The transport gives std::byte, the codec takes uint8_t.
std::span<const uint8_t> AsUint8(std::span<const std::byte> data);

}  // namespace z13::net
