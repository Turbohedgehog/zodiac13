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

#include <console_input/console_variables.h>

#include <algorithm>
#include <format>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <z13_settings/settings.h>

namespace z13::console_input {

namespace {

constexpr char kPathSeparator = '.';

std::string Shown(const SettingEntry& entry) {
  return std::format("{} = {}", entry.path, entry.value);
}

std::string ShownAll(const std::vector<SettingEntry>& entries) {
  std::string text;
  for (const SettingEntry& entry : entries) {
    text += std::format("{}{}", text.empty() ? "" : "\n", Shown(entry));
  }
  return text;
}

}  // namespace

std::optional<CommandResult> RunVariable(flecs::world world, std::span<const std::string> words) {
  if (words.empty()) {
    return std::nullopt;
  }
  const std::string& path = words.front();
  const std::vector<SettingEntry> matching = ListSettings(SettingsOf(world), path);
  const auto exact = std::ranges::find(matching, path, &SettingEntry::path);
  if (exact == matching.end()) {
    const std::string group = path.ends_with(kPathSeparator) ? path : std::format("{}{}", path, kPathSeparator);
    const std::vector<SettingEntry> members = ListSettings(SettingsOf(world), group);
    if (members.empty()) {
      return std::nullopt;
    }
    if (words.size() > 1) {
      return CommandResult(std::unexpected(std::format("'{}' is a group of settings, not one: name a setting", path)));
    }
    return CommandResult(rfl::Generic(ShownAll(members)));
  }
  if (words.size() > 2) {
    return CommandResult(std::unexpected(std::format("{} takes one value, got {}", path, words.size() - 1)));
  }
  if (words.size() == 2) {
    if (const Status changed = ChangeSetting(world, path, words[1]); !changed) {
      return CommandResult(std::unexpected(changed.error()));
    }
    const std::vector<SettingEntry> now = ListSettings(SettingsOf(world), path);
    const auto current = std::ranges::find(now, path, &SettingEntry::path);
    return CommandResult(rfl::Generic(current == now.end() ? std::string() : Shown(*current)));
  }
  return CommandResult(rfl::Generic(Shown(*exact)));
}

}  // namespace z13::console_input
