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

#include "game_transitions.h"

#include <string>
#include <string_view>

#include <z13/components/gameplay/gameplay.h>
#include <z13/components/gameplay/net.h>

#include "pending_transition.h"

namespace z13::raylib::gui {

namespace {

constexpr std::string_view kStartingGameLabel = "Starting the game...";
constexpr std::string_view kLeavingGameLabel = "Leaving the game...";

}  // namespace

void StartGame(flecs::world world, const GameScene& scene) {
  world.set<PendingTransition>({.label = std::string(kStartingGameLabel), .action = [scene](flecs::world world) {
    world.set<z13::net::ConnectionStatus>({});
    SelectScene(world, scene);
    world.add<z13::gameplay::Gameplay>();
    world.remove<z13::gameplay::Pause>();
  }});
}

void LeaveGame(flecs::world world) {
  // Pause stays set, so GuiSystem::Draw shows the main menu once Gameplay is gone.
  world.set<PendingTransition>({.label = std::string(kLeavingGameLabel), .action = [](flecs::world world) {
    world.remove<z13::gameplay::Gameplay>();
  }});
}

}  // namespace z13::raylib::gui
