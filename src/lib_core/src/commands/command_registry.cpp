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


#include <lib_core/commands/command_registry.h>
#include <lib_core/time/simulation_clock.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>

namespace z13 {

namespace {

constexpr std::string_view kCommandSuffix = "Command";

bool IsUpper(char c) {
  return std::isupper(static_cast<unsigned char>(c)) != 0;
}

}  // namespace

std::string CommandNameOf(std::string_view type_name) {
  if (type_name.size() > kCommandSuffix.size() && type_name.ends_with(kCommandSuffix)) {
    type_name.remove_suffix(kCommandSuffix.size());
  }
  std::string name;
  for (const char c : type_name) {
    if (IsUpper(c)) {
      if (!name.empty()) {
        name += '_';
      }
      name += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    } else {
      name += c;
    }
  }
  return name;
}

void EnsureCommands(flecs::world& world) {
  if (world.has<Commands>()) {
    return;
  }
  world.component<CommandRequest>();
  // Presentation phases run every frame, also while the simulation is frozen (menu, pause), when a console or a
  // channel still has to be answered.
  world.component<CommandPhase>().add(flecs::Phase).add<flecs_tools::PresentationPhase>().depends_on(flecs::PreFrame);
  world.component<CommandCollectPhase>().add(flecs::Phase).add<flecs_tools::PresentationPhase>().depends_on<CommandPhase>();
  world.component<Commands>().add(flecs::Singleton);
  world.set<Commands>({});
  // A request created after this frame's handlers ran has no reply yet: it waits one frame.
  world.system<CommandRequest, Commands>("CommandCollect")
      .kind<CommandCollectPhase>()
      .each([](flecs::entity request, CommandRequest& state, Commands& commands) {
        const auto pending = commands.pending.find(state.ticket);
        if (pending == commands.pending.end()) {
          request.destruct();
          return;
        }
        const CommandEntry& entry = commands.entries.at(pending->second.name);
        std::optional<CommandResult> reply = entry.collect(request);
        if (!reply && state.age++ == 0) {
          return;
        }
        CommandCallback callback = std::move(pending->second.callback);
        commands.pending.erase(pending);
        request.destruct();
        callback(reply ? std::move(*reply)
                       : CommandResult(std::unexpected(std::format("nothing answered '{}'", entry.name))));
      });
}

const CommandEntry* FindCommand(flecs::world world, const CommandFilter& filter, std::string_view name) {
  if (!world.has<Commands>()) {
    return nullptr;
  }
  const auto& entries = world.get<Commands>().entries;
  const auto found = entries.find(name);
  if (found == entries.end() ||
      !std::ranges::any_of(found->second.tags, [&filter](const std::string& tag) {
        return std::ranges::find(filter.tags, tag) != filter.tags.end();
      })) {
    return nullptr;
  }
  return &found->second;
}

Status SubmitCommand(flecs::world world, const CommandFilter& filter, std::string_view name,
                     const rfl::Generic& arguments, CommandCallback callback) {
  const CommandEntry* entry = FindCommand(world, filter, name);
  if (entry == nullptr) {
    return std::unexpected(std::format("unknown command '{}'", name));
  }
  if (entry->handlers == 0) {
    return std::unexpected(std::format("no handler for '{}'", name));
  }
  const auto request = entry->create(world, arguments);
  if (!request) {
    return std::unexpected(request.error());
  }
  Commands& commands = world.get_mut<Commands>();
  const std::uint64_t ticket = commands.next_ticket++;
  request->set<CommandRequest>({.ticket = ticket});
  commands.pending.emplace(ticket, PendingCommand {.name = entry->name, .callback = std::move(callback)});
  return {};
}

std::vector<std::string> CommandsWithoutHandler(flecs::world world) {
  std::vector<std::string> names;
  if (!world.has<Commands>()) {
    return names;
  }
  for (const auto& [name, entry] : world.get<Commands>().entries) {
    if (entry.handlers == 0) {
      names.push_back(name);
    }
  }
  return names;
}

}  // namespace z13
