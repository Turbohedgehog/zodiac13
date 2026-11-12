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


#include "mcp_commands.h"

#include <string>
#include <vector>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>

#include "mcp_filter.h"

namespace z13::mcp {

namespace {

struct PingCommand {
  using Command = z13::Command<std::string, "Answers pong: checks the connection.", Tags<"debug">>;

  int unused {};  // a command takes its arguments from fields; this one takes none
};

struct CommandFieldInfo {
  std::string name;
  std::string kind;
};

struct CommandInfo {
  std::string name;
  std::string description;
  std::vector<std::string> tags;
  std::vector<CommandFieldInfo> fields;
};

struct CommandsCommand {
  using Command =
      z13::Command<std::vector<CommandInfo>, "Lists the commands this channel shows: name, description, tags, fields.",
                   Tags<"debug">>;

  int unused {};
};

std::string KindName(FieldKind kind) {
  switch (kind) {
    case FieldKind::kBool:
      return "bool";
    case FieldKind::kInteger:
      return "integer";
    case FieldKind::kNumber:
      return "number";
    case FieldKind::kText:
      return "text";
    case FieldKind::kOther:
      break;
  }
  return "other";
}

std::vector<CommandInfo> Listed(flecs::world world) {
  const CommandFilter filter = FilterOf(world.get<z13::McpServerSettings>());
  std::vector<CommandInfo> listed;
  for (const auto& [name, entry] : world.get<Commands>().entries) {
    if (FindCommand(world, filter, name) == nullptr) {
      continue;
    }
    CommandInfo info {.name = name, .description = entry.description, .tags = entry.tags};
    for (const CommandField& field : entry.fields) {
      info.fields.push_back({.name = field.name, .kind = KindName(field.kind)});
    }
    listed.push_back(std::move(info));
  }
  return listed;
}

}  // namespace

void RegisterMcpCommandComponents(flecs::world world) {
  flecs_tools::RegisterComponents<PingCommand, CommandsCommand>(world);
}

void RegisterMcpCommandSystems(flecs::world world) {
  CommandSystem<PingCommand>(world, "McpCommands::Ping",
                             [](const PingCommand&, ReplyOf<PingCommand>& reply) { reply.Succeed("pong"); });
  CommandSystem<CommandsCommand>(world, "McpCommands::List",
                                 [](flecs::iter& it, size_t, const CommandsCommand&, ReplyOf<CommandsCommand>& reply) {
                                   reply.Succeed(Listed(it.world()));
                                 });
}

}  // namespace z13::mcp
