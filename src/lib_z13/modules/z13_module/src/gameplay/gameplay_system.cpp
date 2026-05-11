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

#include "gameplay_system.h"

#include <string>

#include <flecs.h>
#include <Eigen/Dense>

#include <z13/components/gameplay.h>
#include <z13/components/input.h>

#include <lib_core/log.h>

namespace z13::gameplay {

// todo: убрать константу и брать из z13.fbs.actions.Action.action_group
static const std::string kBuildingActionGroup = "Control";

void RegisterPipeline(flecs::world& world) {
  world.component<PreUpdatePhase>().add(flecs::Phase).depends_on(flecs::OnUpdate);
  world.component<UpdatePhase>().add(flecs::Phase).depends_on<PreUpdatePhase>();
  world.component<PostUpdatePhase>().add(flecs::Phase).depends_on<UpdatePhase>();
  world.get_alive(flecs::OnValidate).add(flecs::Phase).depends_on<PostUpdatePhase>();
}

void UpdateGameplay() {
  // LOG_INFO("UpdateGameplay()");
}

void OnGameplay() {
  // LOG_INFO("Gameplay()");
}

void ValidateGameplay() {
  // LOG_INFO("ValidateGameplay()");
}

void CreateTestPlayer(flecs::world world, gameplay::Gameplay& gameplay) {
  auto test_actor_entity = world.entity("TestPlayer");
  Camera camera {
    .fov = 90,
    .name = "TestActorCamera",
  };

  Player player {
    .id = gameplay.last_registered_player_id++,
  };

  Eigen::Matrix4f camera_transform = Eigen::Matrix4f::Identity();
  z13::input::ActionListener action_listener {
    .action_group_priority = { kBuildingActionGroup },
  };

  test_actor_entity
      .set(std::move(camera))
      .set(std::move(camera_transform))
      .set(std::move(player))
      // todo: перенести добавление компонент в input_system
      .add<z13::input::InputListener>()
      .set(std::move(action_listener))
      .add<z13::input::CurrentActionListenerTag>();
}

void OnInit(flecs::iter it, size_t /*i*/, gameplay::Gameplay& gameplay) {
  CreateTestPlayer(it.world(), gameplay);

  LOG_INFO("~~~~ gameplay::OnInit");
}

void GameplaySystem::Register(flecs::world& world) {
  RegisterPipeline(world);

  world.observer<gameplay::Gameplay>("GameplaySystem::OnInit")
    .event(flecs::OnAdd)
    .yield_existing()
    // .each([world](const auto& gameplay) { OnInit(world, gameplay); });
    .each(OnInit);

  world.observer<const WindowFocusEvent>("WindowFocusEvent::OnSet")
    .event(flecs::OnSet)
    .yield_existing()
    .each([world](const auto& window_focus_event) {
      if (!window_focus_event.has_focus) {
        world.add<Pause>();
      }
    });

  world.system("UpdateGameplaySystem")
    .kind<UpdatePhase>()
    .each(UpdateGameplay);

  world.system("OnGameplaySystem")
    .kind<UpdatePhase>()
    .each(OnGameplay);

  world.system("ValidateGameplaySystem")
    .kind(flecs::OnValidate)
    .each(ValidateGameplay);
}

}  // namespace z13::gameplay
