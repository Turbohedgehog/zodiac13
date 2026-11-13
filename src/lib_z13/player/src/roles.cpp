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

#include "player/roles.h"

#include <algorithm>
#include <format>
#include <ranges>
#include <unordered_set>
#include <vector>

#include <rfl.hpp>
#include <rfl/json.hpp>

namespace z13::gameplay {

namespace {

struct RoleJson {
  std::string name;
  std::string title;
  std::vector<std::string> access;
  uint32_t slots {};
};

struct RolesJson {
  std::vector<RoleJson> roles;
};

std::unexpected<std::string> Error(std::string message) {
  return std::unexpected(std::format("roles: {}", message));
}

z13::inventory::PlayerRole HeldBy(const z13::inventory::Role& role) {
  return {.name = role.name, .access = role.access};
}

}  // namespace

std::filesystem::path RolesFile() {
  return std::filesystem::path(kRolesFile);
}

std::expected<z13::inventory::Roles, std::string> ParseRoles(std::string_view json) {
  const auto parsed = rfl::json::read<RolesJson, rfl::DefaultIfMissing>(std::string(json));
  if (!parsed) {
    return Error(parsed.error().what());
  }
  z13::inventory::Roles roles;
  std::unordered_set<std::string> names;
  for (const RoleJson& entry : parsed->roles) {
    if (entry.name.empty() || !names.insert(entry.name).second) {
      return Error(std::format("'{}' is nameless or repeats", entry.name));
    }
    z13::inventory::AccessSet access {};
    for (const std::string& name : entry.access) {
      const auto known = z13::inventory::ParseAccess(name);
      if (!known) {
        return Error(std::format("post '{}': no access '{}'", entry.name, name));
      }
      access |= z13::inventory::AccessBit(*known);
    }
    roles.roles.push_back({.name = entry.name, .title = entry.title, .access = access, .slots = entry.slots});
  }
  if (!std::ranges::any_of(roles.roles, [](const z13::inventory::Role& role) { return role.slots == 0; })) {
    return Error("no post without a limit of places");
  }
  return roles;
}

z13::inventory::PlayerRole AssignRole(const z13::inventory::Roles& roles, std::span<const std::string> desired,
                                      std::span<const std::string> taken) {
  const auto has_place = [&taken](const z13::inventory::Role& role) {
    return role.slots == 0 || std::ranges::count(taken, role.name) < role.slots;
  };
  for (const std::string& name : desired) {
    const auto role = std::ranges::find(roles.roles, name, &z13::inventory::Role::name);
    if (role != roles.roles.end() && has_place(*role)) {
      return HeldBy(*role);
    }
  }
  const auto open = std::ranges::find(roles.roles, 0u, &z13::inventory::Role::slots);
  return open == roles.roles.end() ? z13::inventory::PlayerRole {} : HeldBy(*open);
}

std::vector<std::string> SplitRoleNames(std::string_view list) {
  std::vector<std::string> names;
  for (const auto part : std::views::split(list, ',')) {
    std::string name(part.begin(), part.end());
    std::erase(name, ' ');
    if (!name.empty()) {
      names.push_back(std::move(name));
    }
  }
  return names;
}

void GiveRole(flecs::entity player, std::span<const std::string> desired, std::span<const std::string> also_taken) {
  flecs::world world = player.world();
  const auto* roles = world.try_get<z13::inventory::Roles>();
  if (roles == nullptr) {
    return;
  }
  std::vector<std::string> taken(also_taken.begin(), also_taken.end());
  world.each([&taken](const z13::inventory::PlayerRole& held) { taken.push_back(held.name); });
  player.set(AssignRole(*roles, desired, taken));
}

}  // namespace z13::gameplay
