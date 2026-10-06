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
#include <string>

#include <Eigen/Dense>

#include <flecs.h>

namespace z13::gameplay {

std::string PlayerEntityName(uint32_t id);

// Deterministic per-player spawn offset along X, so two players' octahedra never overlap;
// used when the scene has no SpawnPoint.
constexpr float kSpawnSpacing = 3.f;

// No input/locality components -- those are attached separately (see LocalPlayer).
// Public so net_module's join handling spawns players the same way host startup does.
// `reserved` are spawn positions taken by players not in the world yet.
flecs::entity SpawnPlayer(flecs::world world, uint32_t id, std::span<const Eigen::Vector3f> reserved = {});

// For callers that need the local player ready before the next progress(), unlike the
// per-frame re-derivation in gameplay_input_system.cpp. A no-op if there is none.
void EnsureLocalPlayerReady(flecs::world world);

}  // namespace z13::gameplay
