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

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <flecs.h>
#include <rfl.hpp>

#include <lib_core/commands/command.h>
#include <lib_core/commands/command_field.h>
#include <lib_core/utils/status.h>

namespace z13 {

// A command's arguments and reply as plain data, which every channel can make and read.
using CommandResult = std::expected<rfl::Generic, std::string>;
using CommandCallback = std::function<void(CommandResult)>;

// What a registered command is, as a channel sees it. Runtime only, never state.
struct CommandEntry {
  std::string name;
  flecs::entity_t component {};  // the request's component
  std::string description;
  std::vector<std::string> tags;
  std::vector<CommandField> fields;
  int handlers {};  // systems made by CommandSystem
  // Makes the request's entity from an object of the arguments by field name; a missing one keeps its default.
  std::function<std::expected<flecs::entity, std::string>(flecs::world, const rfl::Generic&)> create;
  std::function<std::optional<CommandResult>(flecs::entity)> collect;
};

// The tags a channel shows: a command matches when it shares one. One without tags matches no channel.
struct CommandFilter {
  std::vector<std::string> tags;
};

struct PendingCommand {
  std::string name;
  CommandCallback callback;
};

// Every command of the world, by name, and the requests waiting for their reply. Runtime only, never state.
struct Commands {
  using Singleton = void;

  std::map<std::string, CommandEntry, std::less<>> entries;
  std::map<std::uint64_t, PendingCommand> pending;
  std::uint64_t next_ticket {1};
};

// "TeleportCommand" -> "teleport", "SetShadows" -> "set_shadows".
std::string CommandNameOf(std::string_view type_name);

// Makes the world's Commands and the phases of the handlers; called by RegisterCommand, safe to repeat.
void EnsureCommands(flecs::world& world);

const CommandEntry* FindCommand(flecs::world world, const CommandFilter& filter, std::string_view name);

// Files a request for a command the filter shows: `callback` gets the reply once the handlers ran, in the
// frame's CommandCollectPhase. Call it from the thread that progresses the world.
Status SubmitCommand(flecs::world world, const CommandFilter& filter, std::string_view name,
                     const rfl::Generic& arguments, CommandCallback callback);

// The commands nothing handles; the launcher warns about them and a test fails on them.
std::vector<std::string> CommandsWithoutHandler(flecs::world world);

namespace commands_detail {

template <class Value>
struct ValueOf {
  using Type = Value;
};

// An optional argument is spelled as the value it may hold; leaving it out leaves it empty.
template <class Value>
struct ValueOf<std::optional<Value>> {
  using Type = Value;
};

template <class Value>
constexpr FieldKind KindOf() {
  if constexpr (!std::is_same_v<typename ValueOf<Value>::Type, Value>) {
    return KindOf<typename ValueOf<Value>::Type>();
  } else if constexpr (std::is_same_v<Value, bool>) {
    return FieldKind::kBool;
  } else if constexpr (std::is_integral_v<Value>) {
    return FieldKind::kInteger;
  } else if constexpr (std::is_floating_point_v<Value>) {
    return FieldKind::kNumber;
  } else if constexpr (std::is_convertible_v<Value, std::string_view>) {
    return FieldKind::kText;
  } else {
    return FieldKind::kOther;
  }
}

template <class T>
std::vector<CommandField> FieldsOf() {
  T sample {};
  std::vector<CommandField> fields;
  rfl::to_view(sample).apply([&fields](const auto& field) {
    fields.push_back({.name = std::string(field.name()), .kind = KindOf<std::remove_cvref_t<decltype(*field.value())>>()});
  });
  return fields;
}

template <class TagList>
std::vector<std::string> TagsOf() {
  return {TagList::kValues.begin(), TagList::kValues.end()};
}

}  // namespace commands_detail

// Adds the command T to the world's Commands; RegisterComponent calls it for a CommandComponentType.
template <CommandComponentType T>
void RegisterCommand(flecs::world& world) {
  using Spec = typename T::Command;
  EnsureCommands(world);
  world.component<ReplyOf<T>>();
  CommandEntry entry {
      .name = CommandNameOf(world.component<T>().name().c_str()),
      .component = world.component<T>().id(),
      .description = std::string(Spec::kText),
      .tags = commands_detail::TagsOf<typename Spec::TagsOf>(),
      .fields = commands_detail::FieldsOf<T>(),
      .create = [](flecs::world create_world,
                   const rfl::Generic& arguments_data) -> std::expected<flecs::entity, std::string> {
        auto arguments = rfl::from_generic<T, rfl::DefaultIfMissing>(arguments_data);
        if (!arguments) {
          return std::unexpected(std::string(arguments.error().what()));
        }
        flecs::entity request = create_world.entity();
        request.set<T>(*arguments);
        request.add<ReplyOf<T>>();
        return request;
      },
      .collect = [](flecs::entity request) -> std::optional<CommandResult> {
        const auto& reply = request.get<ReplyOf<T>>();
        if (!reply.done) {
          return std::nullopt;
        }
        if (reply.failed) {
          return std::unexpected(reply.error);
        }
        return rfl::to_generic(reply.value);
      },
  };
  if constexpr (!std::is_void_v<typename Spec::NameSource>) {
    entry.name = std::string(Spec::NameSource::kValue);
  }
  world.get_mut<Commands>().entries.insert_or_assign(entry.name, std::move(entry));
}

// The system of a command's handler: gets each request of T with its reply to fill. Any number may handle one
// command; each sees what the earlier left in the reply. `configure` gets the builder for .read, .write and
// the like.
template <CommandComponentType T, class Configure, class Handler>
flecs::system CommandSystem(flecs::world& world, const char* name, Configure&& configure, Handler&& handler) {
  const flecs::entity_t component = world.component<T>().id();
  for (auto& entry : world.get_mut<Commands>().entries | std::views::values) {
    if (entry.component == component) {
      ++entry.handlers;
    }
  }
  auto builder = world.system<const T, ReplyOf<T>>(name);
  builder.template kind<CommandPhase>();
  configure(builder);
  return builder.each(std::forward<Handler>(handler));
}

template <CommandComponentType T, class Handler>
flecs::system CommandSystem(flecs::world& world, const char* name, Handler&& handler) {
  return CommandSystem<T>(world, name, [](auto&) {}, std::forward<Handler>(handler));
}

}  // namespace z13
