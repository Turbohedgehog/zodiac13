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


#include "mcp_filter.h"

#include <algorithm>
#include <ranges>
#include <string>

namespace z13::mcp {

namespace {

constexpr char kTagSeparator = ',';

}  // namespace

CommandFilter FilterOf(const z13::McpServerSettings& settings) {
  CommandFilter filter;
  for (const auto part : std::views::split(settings.tags, kTagSeparator)) {
    std::string tag(part.begin(), part.end());
    std::erase_if(tag, [](char c) { return c == ' '; });
    if (!tag.empty()) {
      filter.tags.push_back(std::move(tag));
    }
  }
  return filter;
}

}  // namespace z13::mcp
