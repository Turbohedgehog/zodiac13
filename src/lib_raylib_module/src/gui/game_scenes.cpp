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

#include "game_scenes.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string_view>

#include <primitives/station_assets.h>

#include "../tools/asset_path.h"

namespace z13::raylib::gui {

namespace {

constexpr std::string_view kSpaceshipLabel = "Spaceship";
constexpr std::string_view kNewStationLabel = "New station";

}  // namespace

std::vector<GameScene> GameScenes() {
  std::vector<GameScene> scenes {{.label = std::string(kSpaceshipLabel)}};
  const std::vector<std::string> blueprints =
      z13::building::primitives::BlueprintScenes(AssetPath(std::filesystem::path {}));
  std::ranges::transform(blueprints, std::back_inserter(scenes), [](const std::string& blueprint) {
    return GameScene {.label = blueprint, .station = z13::station::StationSceneChoice {.scene = blueprint}};
  });
  scenes.push_back({.label = std::string(kNewStationLabel), .station = z13::station::StationSceneChoice {}});
  return scenes;
}

void SelectScene(flecs::world world, const GameScene& scene) {
  if (scene.station) {
    world.add<z13::station::StationMode>();
    world.set(*scene.station);
  } else {
    world.remove<z13::station::StationMode>();
    world.remove<z13::station::StationSceneChoice>();
  }
}

}  // namespace z13::raylib::gui
