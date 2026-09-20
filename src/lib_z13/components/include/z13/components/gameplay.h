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
#include <filesystem>
#include <string>

namespace z13::gameplay {

struct PreUpdatePhase {};
struct UpdatePhase {};
struct PostUpdatePhase {};

// Runtime singleton marking an initialized gameplay; holds no world state.
struct Gameplay {
  using Singleton = void;
};

// Monotonic id counters; a singleton that is world state.
struct IdCounters {
  using State = void;
  using Singleton = void;
  uint32_t last_player_id {};
  uint32_t last_block_id {};
};

struct Pause {
  using Singleton = void;
};

struct WindowFocusEvent {
  bool has_focus = false;
};

struct Player {
  using State = void;
  uint32_t id {};
};

struct Camera {
  using State = void;
  float fov = 90.f;
  std::string name;
};

// Where the quick save/load actions read and write the scene; runtime settings, not state.
struct QuickSaveSettings {
  using Singleton = void;
  std::filesystem::path path;
};

constexpr float kPlayerColliderRadius = 0.4f;

struct PlayerCollider {
  using State = void;
  float radius {};
};

}  // namespace z13::gameplay
