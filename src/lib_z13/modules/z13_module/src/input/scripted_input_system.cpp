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

#include "scripted_input_system.h"

#include <flecs.h>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/file_io.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/utils/log.h>
#include <lib_core/world/lifecycle.h>
#include <z13/components/gameplay.h>
#include <z13/components/input_phases.h>
#include <z13/components/input_state.h>
#include <z13/components/player.h>
#include <z13_module/gameplay/gameplay_entities.h>

namespace z13::gameplay::input {

namespace {

// A script that can't be read stops the run: playing nothing would look like a quiet client.
void LoadScript(flecs::world& world) {
  const auto config = z13::GetCoreConfig(world);
  const auto path = config ? config->get().GetInputScriptPath() : std::nullopt;
  if (!path) {
    return;
  }
  auto steps = z13::ReadFile(*path).and_then(ParseInputScript);
  if (!steps) {
    log_error("Input script {}: {}", path->string(), steps.error());
    z13::ShutdownCore(world, 1);
    return;
  }
  log_info("Input script {}: {} steps", path->string(), steps->size());
  world.set(ScriptedInput {.steps = std::move(*steps)});
}

bool LocalPlayerInGame(flecs::world world) {
  const std::optional<uint32_t> id = world.get<LocalPlayer>().id;
  return world.has<Gameplay>() && id && world.lookup(PlayerEntityName(*id).c_str());
}

void ApplyStep(flecs::world world, const ScriptStep& step, z13::input::InputState& input_state) {
  switch (step.action) {
    case ScriptAction::kKeyDown:
      input_state.pending.push_back({.key = *step.key, .value = 1.f});
      break;
    case ScriptAction::kKeyUp:
      input_state.pending.push_back({.key = *step.key, .value = 0.f});
      break;
    case ScriptAction::kTurn:
      input_state.mouse_yaw_delta_deg += step.turn_degrees.x();
      input_state.mouse_pitch_delta_deg += step.turn_degrees.y();
      break;
    case ScriptAction::kQuit:
      log_info("Input script: done, quitting");
      z13::ShutdownCore(world, 0);
      break;
  }
}

void PlayScript(flecs::iter& it, size_t, ScriptedInput& script, z13::input::InputState& input_state,
                const z13::flecs_tools::SimulationClock& clock) {
  flecs::world world = it.world();
  if (!script.start_tick) {
    if (!LocalPlayerInGame(world)) {
      return;
    }
    script.start_tick = clock.tick;
    log_info("Input script: the local player is in the game at tick {}", clock.tick);
  }
  for (; script.next < script.steps.size() && *script.start_tick + script.steps[script.next].tick <= clock.tick;
       ++script.next) {
    ApplyStep(world, script.steps[script.next], input_state);
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponent<ScriptedInput>(world);
}

void RegisterSystems(flecs::world world) {
  LoadScript(world);
  // Before ApplyKeyTransitionsPhase takes the pending transitions, like the platform's events.
  world.system<ScriptedInput, z13::input::InputState, const z13::flecs_tools::SimulationClock>(
           "ScriptedInputSystem::PlayScript")
      .kind<z13::input::ClearActionFramePhase>()
      .without<z13::flecs_tools::ReplayInProgress>()
      .each(PlayScript);
}

}  // namespace

void ScriptedInputSystem::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::gameplay::input
