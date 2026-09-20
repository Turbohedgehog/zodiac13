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
#include <string_view>

namespace z13::testing {

// Replaces the SimulationClock.tick value in a WorldJsonStore JSON with a fixed
// placeholder. Two snapshots that are otherwise identical can still disagree on tick
// -- e.g. a settle frame after WorldJsonStore::Load, or extra frames between a quick
// save and a later re-save -- so tests that check "everything else round-tripped"
// compare through this instead of the raw JSON.
inline std::string WithNormalizedSimulationTick(std::string json) {
  constexpr std::string_view kKey = "\"tick\": ";
  const auto key_pos = json.find(kKey);
  if (key_pos == std::string::npos) {
    return json;
  }
  const auto digits_start = key_pos + kKey.size();
  const auto digits_end = json.find(',', digits_start);
  json.replace(digits_start, digits_end - digits_start, "0");
  return json;
}

}  // namespace z13::testing
