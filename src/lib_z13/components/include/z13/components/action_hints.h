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
#include <deque>
#include <optional>
#include <utility>

#include <boost/container/flat_map.hpp>

#include <z13/components/action_id.h>

namespace z13::gameplay {

// Never part of the logged input: a hint only stands in for a value not yet confirmed, and is
// dropped once the confirmed commands reach its tick (docs/action-delivery-plan.md).
struct ActionHint {
  uint64_t tick {};
  float value {};
};

struct ActionHints {
  using Singleton = void;
  using SessionScoped = void;

  static constexpr size_t kKeptPerAction = 8;

  boost::container::flat_map<std::pair<uint32_t, z13::input::ActionId>, std::deque<ActionHint>> by_action;

  // Older than the newest kept for the action are dropped as stale, like an unreliable packet that
  // arrives out of order.
  void Add(uint32_t player_id, z13::input::ActionId action_id, const ActionHint& hint) {
    auto& kept = by_action[{player_id, action_id}];
    if (!kept.empty() && hint.tick <= kept.back().tick) {
      return;
    }
    kept.push_back(hint);
    if (kept.size() > kKeptPerAction) {
      kept.pop_front();
    }
  }

  // The latest hint at or before `tick` that is past `confirmed_through`, which the confirmed
  // commands speak for.
  std::optional<float> ValueAt(
      uint32_t player_id, z13::input::ActionId action_id, uint64_t tick, uint64_t confirmed_through) const {
    const auto found = by_action.find({player_id, action_id});
    if (found == by_action.end()) {
      return std::nullopt;
    }
    std::optional<float> value;
    for (const ActionHint& hint : found->second) {
      if (hint.tick > tick) {
        break;
      }
      if (hint.tick > confirmed_through) {
        value = hint.value;
      }
    }
    return value;
  }
};

}  // namespace z13::gameplay
