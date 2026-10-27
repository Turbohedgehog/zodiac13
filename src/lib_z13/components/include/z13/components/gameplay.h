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

namespace z13::gameplay {

// Emitted while a new scene starts on a host (not a client), before the local player
// spawns, so other modules can add scene content such as spawn points.
struct PopulateSceneEvent {};

// Runtime singleton marking an initialized gameplay; holds no world state.
struct Gameplay {
  using Singleton = void;
};

// The next player id to hand out; world state.
struct IdCounters {
  using State = void;
  using Singleton = void;
  uint32_t last_player_id {};
};

struct Pause {
  using Singleton = void;
};

// The mouse is free to use the GUI while the game runs: look stops, the cursor shows.
struct FreeCursor {
  using Singleton = void;
};

struct WindowFocusEvent {
  bool has_focus = false;
};

// Where the quick save/load actions read and write the scene; runtime settings, not state.
struct QuickSaveSettings {
  using Singleton = void;
  std::filesystem::path path;
};

}  // namespace z13::gameplay
