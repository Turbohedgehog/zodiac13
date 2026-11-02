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
#include <span>
#include <unordered_map>

#include <Eigen/Dense>
#include <flecs.h>

#include <lib_core/utils/camera_pose.h>

namespace z13::net {

struct GhostPose {
  Eigen::Vector3f position = Eigen::Vector3f::Zero();
  z13::LookAngles look;
  uint64_t tick {};
};

struct GhostPlayers {
  using Singleton = void;
  using SessionScoped = void;
  std::unordered_map<uint32_t, GhostPose> by_player;
};

// A pose packet from the server; poses of the local player or of a player the client simulates are skipped.
void StoreGhostPoses(flecs::world world, std::span<const uint8_t> bytes);

class GhostPlayerSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::net
