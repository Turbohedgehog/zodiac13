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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>

#include <lib_core/utils/asset_source.h>

#include <z13/components/station_mode.h>

namespace z13::raylib::gui {

// The scene without a station blueprint.
inline constexpr std::string_view kSpaceshipLabel = "Spaceship";

// A scene a local or hosted game starts in; the spaceship has no station.
struct GameScene {
  std::string label;
  std::optional<z13::station::StationSceneChoice> station;
};

// The default station (kDefaultScene) first, then the spaceship, the other station blueprints
// under assets/station/blueprints/ and a new, empty station.
std::vector<GameScene> GameScenes(const AssetSource& assets);

// Puts the world in `scene`'s mode (StationMode, StationSceneChoice) for the game to start in.
void SelectScene(flecs::world world, const GameScene& scene);

}  // namespace z13::raylib::gui
