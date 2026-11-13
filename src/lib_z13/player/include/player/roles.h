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

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>

#include <z13/components/gameplay/role.h>

namespace z13::gameplay {

// Header constant: this library is linked into several binaries, and check_duplicated_state.py allows only these.
inline constexpr std::string_view kRolesFile = "station/roles.json";

// Relative to assets/.
std::filesystem::path RolesFile();

// The posts of roles.json: names are unique, access names are z13::inventory::AccessName's, and one post has no
// limit of places, so that a joiner always gets something.
std::expected<z13::inventory::Roles, std::string> ParseRoles(std::string_view json);

// The first of `desired` with a place left (`taken` lists the posts the players hold); with none, the first post of
// no limit. Server-side: the player asks, the server decides.
z13::inventory::PlayerRole AssignRole(const z13::inventory::Roles& roles, std::span<const std::string> desired,
                                      std::span<const std::string> taken);

// A comma-separated list of post names (the CrewChoice setting), blanks dropped.
std::vector<std::string> SplitRoleNames(std::string_view list);

// Gives `player` the post AssignRole picks among the posts of the players in the world and of `also_taken` (joins
// still on their way); without a Roles table the player has none.
void GiveRole(flecs::entity player, std::span<const std::string> desired, std::span<const std::string> also_taken = {});

}  // namespace z13::gameplay
