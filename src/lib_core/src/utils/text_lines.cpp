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

#include <lib_core/utils/text_lines.h>

#include <algorithm>

namespace z13 {

namespace {

constexpr char kLineFeed = '\n';
constexpr char kCarriageReturn = '\r';

}  // namespace

std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> lines;
  while (!text.empty()) {
    const size_t end = text.find(kLineFeed);
    std::string_view line = text.substr(0, end);
    if (!line.empty() && line.back() == kCarriageReturn) {
      line.remove_suffix(1);
    }
    lines.emplace_back(line);
    text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
  }
  return lines;
}

std::string JoinLines(std::span<const std::string> lines, size_t first, size_t last) {
  if (lines.empty()) {
    return {};
  }
  const size_t newest = lines.size() - 1;
  const auto [low, high] = std::minmax(std::min(first, newest), std::min(last, newest));
  std::string joined;
  for (const std::string& line : lines.subspan(low, high - low + 1)) {
    joined += line;
    joined += kLineFeed;
  }
  return joined;
}

}  // namespace z13
