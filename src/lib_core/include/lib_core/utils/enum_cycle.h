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

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <type_traits>

#include <rfl.hpp>

namespace z13 {

// The enumerator after `value` by value, the first after the last: a key that steps through
// modes needs no change when a mode is added.
template <typename Enum>
  requires std::is_enum_v<Enum>
Enum NextEnumerator(Enum value) {
  constexpr auto kEnumerators = rfl::get_enumerator_array<Enum>();
  const auto found = std::ranges::find(kEnumerators, value, [](const auto& enumerator) { return enumerator.second; });
  const auto index = static_cast<size_t>(std::distance(kEnumerators.begin(), found));
  return kEnumerators[(index + 1) % kEnumerators.size()].second;
}

}  // namespace z13
