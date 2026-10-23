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
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace z13 {

// The lines of `text`, without their line ends; a final line end doesn't start another line.
std::vector<std::string> SplitLines(std::string_view text);

// The lines `first` to `last` (both included, in either order, clamped to the lines there are),
// each ended by a line feed; nothing when there are no lines.
std::string JoinLines(std::span<const std::string> lines, size_t first, size_t last);

}  // namespace z13
