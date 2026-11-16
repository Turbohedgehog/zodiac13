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


#include "console_commands.h"

#include <format>
#include <optional>
#include <string>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/console_log.h>
#include <lib_core/utils/flecs_utils.h>
#include <lib_core/world/lifecycle.h>
#include <z13/components/console/assistant_chat.h>

#include <console_input/console_session.h>

namespace z13 {

namespace {

struct HelpCommand {
  using Command = z13::Command<std::string,
                               "Lists the commands of the console with what each takes; with `name`, that command "
                               "alone. A command is typed as its name and then its arguments, by position or as "
                               "name=value; a word with spaces goes in \"quotes\".",
                               Tags<console_input::kConsoleTag>>;

  std::optional<std::string> name;
};

struct ClearCommand {
  using Command = z13::Command<std::string, "Empties the log the console shows.", Tags<console_input::kConsoleTag>>;

  int unused {};
};

struct QuitCommand {
  using Command = z13::Command<std::string, "Closes the game.", Tags<console_input::kConsoleTag>>;

  int unused {};
};

struct ExitCommand {
  using Command = z13::Command<std::string, "Closes the game, like quit.", Tags<console_input::kConsoleTag>>;

  int unused {};
};

struct EchoCommand {
  using Command = z13::Command<std::string, "Answers with `text`.", Tags<console_input::kConsoleTag>>;

  std::string text;
};

struct ConsoleSayCommand {
  using Command = z13::Command<std::string,
                               "Writes `text` in the console's assistant tab as the assistant's reply to the player.",
                               Tags<kDebugTag>>;

  std::string text;
};

struct ConsoleSeenCommand {
  using Command = z13::Command<std::string,
                               "Marks what the player said in the assistant tab as seen, without an answer.",
                               Tags<kDebugTag>>;

  int unused {};
};

std::string UsageOf(const std::string& name, const CommandEntry& entry) {
  std::string usage = name;
  for (const CommandField& field : entry.fields) {
    usage += std::format(" <{}>", field.name);
  }
  return usage;
}

std::string FieldsOf(const CommandEntry& entry) {
  std::string fields;
  for (const CommandField& field : entry.fields) {
    fields += std::format("\n  {}: {}", field.name, FieldKindName(field.kind));
  }
  return fields;
}

void RegisterComponents(flecs::world world) {
  flecs_tools::RegisterComponents<console::AssistantChat, HelpCommand, ClearCommand, QuitCommand, ExitCommand,
                                  EchoCommand, ConsoleSayCommand, ConsoleSeenCommand>(world);
}

void RegisterSystems(flecs::world world) {
  world.set<console::AssistantChat>({});
  CommandSystem<HelpCommand>(
      world, "ConsoleCommands::Help",
      [](flecs::iter& it, size_t, const HelpCommand& command, ReplyOf<HelpCommand>& reply) {
        flecs::world world = it.world();
        const CommandFilter filter = console_input::ConsoleFilter();
        if (command.name) {
          const CommandEntry* entry = FindCommand(world, filter, *command.name);
          if (entry == nullptr) {
            reply.Fail(std::format("unknown command '{}'", *command.name));
            return;
          }
          reply.Succeed(std::format("{}\n{}{}", UsageOf(*command.name, *entry), entry->description,
                                    FieldsOf(*entry)));
          return;
        }
        std::string listing;
        for (const auto& [name, entry] : world.get<Commands>().entries) {
          if (FindCommand(world, filter, name) != nullptr) {
            listing += std::format("{}{}", listing.empty() ? "" : "\n", UsageOf(name, entry));
          }
        }
        reply.Succeed(std::move(listing));
      });
  CommandSystem<ClearCommand>(
      world, "ConsoleCommands::Clear",
      [](flecs::iter& it, size_t, const ClearCommand&, ReplyOf<ClearCommand>& reply) {
        it.world().get<ConsoleLog>().Clear();
        reply.Succeed("");
      });
  CommandSystem<ConsoleSayCommand>(
      world, "ConsoleCommands::Say", [](auto& builder) { builder.template write<console::AssistantChat>(); },
      [](flecs::iter& it, size_t, const ConsoleSayCommand& command, ReplyOf<ConsoleSayCommand>& reply) {
        console::AssistantSays(it.world().get_mut<console::AssistantChat>(), command.text);
        reply.Succeed("");
      });
  CommandSystem<ConsoleSeenCommand>(
      world, "ConsoleCommands::Seen", [](auto& builder) { builder.template write<console::AssistantChat>(); },
      [](flecs::iter& it, size_t, const ConsoleSeenCommand&, ReplyOf<ConsoleSeenCommand>& reply) {
        console::MarkSeen(it.world().get_mut<console::AssistantChat>());
        reply.Succeed("");
      });
  CommandSystem<QuitCommand>(world, "ConsoleCommands::Quit",
                             [](flecs::iter& it, size_t, const QuitCommand&, ReplyOf<QuitCommand>& reply) {
                               ShutdownCore(it.world(), 0);
                               reply.Succeed("");
                             });
  CommandSystem<ExitCommand>(world, "ConsoleCommands::Exit",
                             [](flecs::iter& it, size_t, const ExitCommand&, ReplyOf<ExitCommand>& reply) {
                               ShutdownCore(it.world(), 0);
                               reply.Succeed("");
                             });
  CommandSystem<EchoCommand>(world, "ConsoleCommands::Echo",
                             [](const EchoCommand& command, ReplyOf<EchoCommand>& reply) {
                               reply.Succeed(command.text);
                             });
}

}  // namespace

void ConsoleCommands::Register(flecs::world& world) {
  OnRegisterComponents(world, RegisterComponents);
  OnInitSystems(world, RegisterSystems);
}

}  // namespace z13
