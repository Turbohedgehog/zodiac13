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

#include "settings_commands.h"

#include <format>
#include <optional>
#include <string>
#include <vector>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/world/lifecycle.h>
#include <z13_settings/settings.h>

namespace z13 {

namespace {

struct SettingsCommand {
  using Command = z13::Command<std::vector<SettingEntry>,
                               "The settings the game runs on, as path and value (render.shadow_lights): all of "
                               "them, or those whose path starts with `prefix`.",
                               Tags<kDebugTag>>;

  std::string prefix;
};

struct SetCommand {
  using Command = z13::Command<std::string,
                               "Changes a number or a flag of the settings for this run, without a restart and "
                               "without saving: `path` as listed by settings, `value` as text. It is checked "
                               "like a command line value.",
                               Tags<kDebugTag, "cheat">>;

  std::string path;
  std::string value;
};

// The upper bound of `fps` in core_settings.fbs: the pace the loop can't hold, so frames run back to back.
constexpr double kUnlimitedFps = 1000.;

struct FrameRateCommand {
  using Command = z13::Command<std::string,
                               "Paces the frame loop and the simulation (ticks per second) for this run: `fps`, "
                               "or `unlimited` for the loop's top rate, to measure the frame time without the "
                               "cap; neither puts back the setting's `core.fps`. Replies with the rate in force.",
                               Tags<kDebugTag, "cheat">>;

  std::optional<double> fps;
  bool unlimited {};
};

void RegisterComponents(flecs::world world) {
  flecs_tools::RegisterComponents<SettingsCommand, SetCommand, FrameRateCommand>(world);
}

void RegisterSystems(flecs::world world) {
  CommandSystem<SettingsCommand>(
      world, "SettingsCommands::List",
      [](flecs::iter& it, size_t, const SettingsCommand& command, ReplyOf<SettingsCommand>& reply) {
        reply.Succeed(ListSettings(SettingsOf(it.world()), command.prefix));
      });
  CommandSystem<SetCommand>(
      world, "SettingsCommands::Set",
      [](flecs::iter& it, size_t, const SetCommand& command, ReplyOf<SetCommand>& reply) {
        if (const auto changed = ChangeSetting(it.world(), command.path, command.value); !changed) {
          reply.Fail(changed.error());
          return;
        }
        reply.Succeed(std::format("{} = {}", command.path, command.value));
      });
  CommandSystem<FrameRateCommand>(
      world, "SettingsCommands::FrameRate",
      [](flecs::iter& it, size_t, const FrameRateCommand& command, ReplyOf<FrameRateCommand>& reply) {
        const auto config = GetCoreConfig(it.world());
        if (!config) {
          reply.Fail("no core in this world");
          return;
        }
        const std::optional<double> pace = command.unlimited ? std::optional(kUnlimitedFps) : command.fps;
        if (pace && (*pace < 1. || *pace > kUnlimitedFps)) {
          reply.Fail(std::format("fps must be in 1..{}", kUnlimitedFps));
          return;
        }
        OverrideCoreFps(it.world(), pace);
        reply.Succeed(std::format("{} fps", config->get().GetFPS()));
      });
}

}  // namespace

void SettingsCommands::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13
