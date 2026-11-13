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

#include <cstdint>
#include <string>
#include <vector>

#include <z13/components/gameplay/access.h>

// Posts of the crew (assets/station/roles.json): the server gives each joining player one, by the posts the player
// asked for and the places left (docs/station-systems-plan.md, "Доступ").
namespace z13::inventory {

// A post; `slots` players at most hold it, zero for any number.
struct Role {
  std::string name;
  std::string title;
  AccessSet access {};
  uint32_t slots {};
};

struct Roles {
  using Singleton = void;
  std::vector<Role> roles;
};

// The post a player holds, with the accesses of its starter pass.
struct PlayerRole {
  using State = void;
  std::string name;
  AccessSet access {};
};

}  // namespace z13::inventory
