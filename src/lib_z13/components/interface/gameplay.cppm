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

export module z13.gameplay;

export import <string>;

export namespace z13::gameplay {

struct PreUpdatePhase {};
struct UpdatePhase {};
struct PostUpdatePhase {};

struct Gameplay {
  uint32_t last_registered_player_id {};
};

struct Pause {};

struct WindowFocusEvent {
  bool has_focus = false;
};

struct Player {
  uint32_t id {};
};

struct Camera {
  float fov = 90.f;
  std::string name;
};

}  // namespace z13::gameplay
