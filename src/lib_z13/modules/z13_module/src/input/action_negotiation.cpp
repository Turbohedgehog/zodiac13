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

#include <z13_module/input/action_negotiation.h>

#include <format>
#include <algorithm>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <lib_core/utils/status.h>

namespace z13::gameplay::input {

namespace {

using z13::input::ActionInfo;
using z13::input::ActionMap;
using z13::input::InputConfig;

struct NewAction {
  const ActionDescriptor* descriptor {};
  ActionInfo::IdType id {};
  ActionInfo::EnumValueType enum_value {};
};

std::optional<std::string> ValidateNames(const ActionDescriptor& action) {
  if (action.enum_name.empty() || action.value_name.empty()) {
    return "an action with an empty name";
  }
  if (action.enum_name.size() > kMaxActionNameLength || action.value_name.size() > kMaxActionNameLength) {
    return std::format("action name '{}:{}' is too long", action.enum_name, action.value_name);
  }
  return std::nullopt;
}

}  // namespace

std::vector<ActionDescriptor> DescribeActions(const ActionMap& action_map) {
  std::vector<ActionDescriptor> described;
  described.reserve(action_map.action_map.size());
  for (const ActionInfo& info : action_map.action_map) {
    described.push_back({
        .enum_name = std::string(info.enum_name),
        .value_name = std::string(info.value_name),
        .enum_value = info.enum_value,
    });
  }
  return described;
}

std::expected<std::vector<uint32_t>, std::string> RegisterRemoteActions(
    ActionMap& action_map, std::span<const ActionDescriptor> actions) {
  if (actions.size() > kMaxActionsPerClient) {
    return std::unexpected(std::format("too many actions ({})", actions.size()));
  }

  const auto& by_name = action_map.action_map.get<ActionMap::EnumActionNameTag>();
  const auto& by_value = action_map.action_map.get<ActionMap::EnumNameEnumValueTag>();
  const auto& by_id = action_map.action_map.get<ActionMap::IdTag>();

  ActionInfo::IdType next_id = by_id.empty() ? 0 : std::prev(by_id.end())->id + 1;
  std::vector<uint32_t> ids;
  std::vector<NewAction> to_register;
  std::map<std::pair<std::string_view, std::string_view>, NewAction> pending_by_name;
  std::set<std::pair<std::string_view, ActionInfo::EnumValueType>> pending_values;
  const auto registered_remotely = static_cast<size_t>(
      std::ranges::count(action_map.action_map, z13::input::kRemoteActionGroup, &ActionInfo::group_name));

  const auto find_registered = [&](std::string_view enum_name,
                                   std::string_view value_name) -> std::optional<ActionInfo::IdType> {
    if (const auto known = by_name.find(std::make_tuple(enum_name, value_name)); known != by_name.end()) {
      return known->id;
    }
    if (const auto added = pending_by_name.find({enum_name, value_name}); added != pending_by_name.end()) {
      return added->second.id;
    }
    return std::nullopt;
  };

  for (const ActionDescriptor& action : actions) {
    if (const auto invalid = ValidateNames(action)) {
      return std::unexpected(*invalid);
    }
    const std::string_view enum_name = action.enum_name;
    const std::string_view value_name = action.value_name;

    // The name is the identity; a differing enum value is the client's own numbering.
    if (const auto registered = find_registered(enum_name, value_name)) {
      ids.push_back(static_cast<uint32_t>(*registered));
      continue;
    }

    if (registered_remotely + to_register.size() >= kMaxRemoteActions || next_id >= kMaxNegotiatedActions) {
      return std::unexpected(std::format("no room left to register action {}:{}", enum_name, value_name));
    }
    // The server never looks remote actions up by value, so a taken one just moves on.
    ActionInfo::EnumValueType enum_value = action.enum_value;
    while (by_value.find(std::make_tuple(enum_name, enum_value)) != by_value.end() ||
           pending_values.contains({enum_name, enum_value})) {
      ++enum_value;
    }
    pending_values.emplace(enum_name, enum_value);
    const NewAction added {.descriptor = &action, .id = next_id++, .enum_value = enum_value};
    pending_by_name.emplace(std::pair {enum_name, value_name}, added);
    to_register.push_back(added);
    ids.push_back(static_cast<uint32_t>(added.id));
  }

  for (const NewAction& added : to_register) {
    const std::string_view enum_name = action_map.owned_names.emplace_back(added.descriptor->enum_name);
    const std::string_view value_name = action_map.owned_names.emplace_back(added.descriptor->value_name);
    action_map.action_map.emplace_back(ActionInfo {
        .enum_name = enum_name,
        .value_name = value_name,
        .group_name = z13::input::kRemoteActionGroup,
        .display_text = {},
        .default_keycodes = {},
        .enum_value = added.enum_value,
        .id = added.id,
    });
  }
  return ids;
}

Status AdoptActionIds(flecs::world world, std::span<const uint32_t> ids) {
  auto& action_map = world.get_mut<ActionMap>();
  if (ids.size() != action_map.action_map.size()) {
    return std::unexpected(std::format(
        "the server returned {} action ids for {} actions", ids.size(), action_map.action_map.size()));
  }
  if (std::unordered_set<uint32_t>(ids.begin(), ids.end()).size() != ids.size()) {
    return std::unexpected(std::string {"the server returned a repeated action id"});
  }

  std::unordered_map<ActionInfo::IdType, ActionInfo::IdType> new_id_of;
  ActionMap::ActionMapContainer renumbered;
  size_t index = 0;
  for (const ActionInfo& info : action_map.action_map) {
    const ActionInfo::IdType new_id = ids[index++];
    new_id_of.emplace(info.id, new_id);
    renumbered.emplace_back(ActionInfo {
        .enum_name = info.enum_name,
        .value_name = info.value_name,
        .group_name = info.group_name,
        .display_text = info.display_text,
        .default_keycodes = info.default_keycodes,
        .absolute = info.absolute,
        .enum_value = info.enum_value,
        .id = new_id,
    });
  }

  auto& input_config = world.get_mut<InputConfig>();
  InputConfig::KeyBindingType rebound;
  for (const auto& binding : input_config.keycode_binding) {
    if (const auto renumbered_id = new_id_of.find(binding.action_id); renumbered_id != new_id_of.end()) {
      rebound.emplace(z13::input::KeyCodeAction {
          .keycode = binding.keycode,
          .action_group = binding.action_group,
          .action_id = renumbered_id->second,
      });
    }
  }

  action_map.action_map.swap(renumbered);
  input_config.keycode_binding.swap(rebound);
  // Their values are keyed by the old ids and refilled every frame.
  world.query_builder<z13::input::ActionListener>().build().each(
      [](z13::input::ActionListener& listener) { listener.action_values.clear(); });

  NotifyConfigUpdated(world);
  return {};
}

void NotifyConfigUpdated(flecs::world world) {
  world.event<z13::input::SystemInputEventType>()
      .id<z13::input::OnConfigUpdatedEvent>()
      .entity(world.entity().add<z13::input::OnConfigUpdatedEvent>())
      .enqueue();
}

}  // namespace z13::gameplay::input
