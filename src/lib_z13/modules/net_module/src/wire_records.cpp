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

#include "wire_records.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <limits>

namespace z13::net {

std::vector<fbs::net::ActionRecordWire> ToWire(const std::vector<z13::gameplay::PlayerActionRecord>& records) {
  std::vector<fbs::net::ActionRecordWire> wire;
  wire.reserve(records.size());
  std::ranges::transform(records, std::back_inserter(wire), [](const z13::gameplay::PlayerActionRecord& record) {
    return fbs::net::ActionRecordWire(
        record.tick, record.player_id, static_cast<uint32_t>(record.action_id), record.value);
  });
  return wire;
}

std::vector<z13::gameplay::PlayerActionRecord> FromWire(const std::vector<fbs::net::ActionRecordWire>& wire) {
  std::vector<z13::gameplay::PlayerActionRecord> records;
  records.reserve(wire.size());
  std::ranges::transform(wire, std::back_inserter(records), [](const fbs::net::ActionRecordWire& record) {
    return z13::gameplay::PlayerActionRecord {
        .tick = record.tick(),
        .player_id = record.player_id(),
        .action_id = record.action_id(),
        .value = record.value(),
    };
  });
  return records;
}

std::unique_ptr<fbs::state::WorldSnapshotT> ToWire(const z13::flecs_tools::WorldSnapshot& snapshot) {
  return std::make_unique<fbs::state::WorldSnapshotT>(z13::flecs_tools::ToFlatbuffer(snapshot));
}

std::expected<CommandRun, std::string> ToCommandRun(
    const std::vector<z13::gameplay::PlayerActionRecord>& records, uint64_t empty_base_tick) {
  CommandRun run {
      .base_tick = records.empty() ? empty_base_tick
                                   : std::ranges::min(records, {}, &z13::gameplay::PlayerActionRecord::tick).tick};
  run.commands.reserve(records.size());
  for (const z13::gameplay::PlayerActionRecord& record : records) {
    if (record.action_id > std::numeric_limits<uint16_t>::max()) {
      return std::unexpected(std::format("action id {} does not fit the wire format", record.action_id));
    }
    const uint64_t delta = record.tick - run.base_tick;
    if (delta > std::numeric_limits<uint8_t>::max()) {
      return std::unexpected(std::format("command {} ticks past the run's base", delta));
    }
    run.commands.emplace_back(
        static_cast<uint8_t>(delta), static_cast<uint16_t>(record.action_id),
        z13::gameplay::QuantizeActionValue(record.value));
  }
  return run;
}

z13::gameplay::PlayerActionRecord FromWire(
    const fbs::net::CommandWire& command, uint64_t base_tick, uint32_t player_id) {
  return {
      .tick = base_tick + command.tick_delta(),
      .player_id = player_id,
      .action_id = command.action_id(),
      .value = z13::gameplay::DequantizeActionValue(command.value()),
  };
}

bool IsKnownActionId(const z13::input::ActionMap& action_map, uint16_t action_id) {
  const auto& by_id = action_map.action_map.get<z13::input::ActionMap::IdTag>();
  return by_id.find(static_cast<z13::input::ActionInfo::IdType>(action_id)) != by_id.end();
}

bool IsAbsoluteAction(const z13::input::ActionMap& action_map, z13::input::ActionInfo::IdType action_id) {
  const auto& by_id = action_map.action_map.get<z13::input::ActionMap::IdTag>();
  const auto info = by_id.find(action_id);
  return info != by_id.end() && info->absolute;
}

std::span<const uint8_t> AsUint8(std::span<const std::byte> data) {
  return {reinterpret_cast<const uint8_t*>(data.data()), data.size()};
}

}  // namespace z13::net
