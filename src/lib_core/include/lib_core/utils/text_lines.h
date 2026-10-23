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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace z13 {

// Without the line ends; a final one doesn't start another line.
std::vector<std::string> SplitLines(std::string_view text);

// Both ends included, in either order, clamped to the lines there are; each line ends with a line feed.
std::string JoinLines(std::span<const std::string> lines, size_t first, size_t last);

// `first` is where the selection started.
struct LineSelection {
  size_t first {};
  size_t last {};

  size_t Low() const { return std::min(first, last); }
  size_t High() const { return std::max(first, last); }
};

// A tap outside a selection grows it to that line, inside it leaves that line, on the one selected
// line clears it; without a selection it does nothing.
std::optional<LineSelection> SelectionAfterTap(const std::optional<LineSelection>& selection, size_t line);

}  // namespace z13
