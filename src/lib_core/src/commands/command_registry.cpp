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

// The value of one argument as a JSON literal.
CommandResultText JsonValue(const CommandArgument& argument, const std::string& token) {
  if (argument.is_text) {
    return rfl::json::write(token);
  }
  const auto parsed = rfl::json::read<rfl::Generic>(token);
  if (!parsed) {
    return std::unexpected(std::format("argument {}: '{}' is not a number or true/false", argument.name, token));
  }
  return rfl::json::write(*parsed);
}

bool Shown(const CommandEntry& entry, const CommandFilter& filter) {
  return std::ranges::any_of(entry.tags, [&filter](const std::string& tag) {
    return std::ranges::find(filter.tags, tag) != filter.tags.end();
  });
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

std::expected<std::vector<std::string>, std::string> SplitCommandLine(std::string_view line) {
  std::vector<std::string> words;
  std::string word;
  bool in_word = false;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '\\') {
      if (++i == line.size()) {
        return std::unexpected("the line ends with a backslash");
      }
      word += line[i];
      in_word = true;
    } else if (c == '"') {
      quoted = !quoted;
      in_word = true;
    } else if (std::isspace(static_cast<unsigned char>(c)) != 0 && !quoted) {
      if (in_word) {
        words.push_back(std::move(word));
        word.clear();
        in_word = false;
      }
    } else {
      word += c;
      in_word = true;
    }
  }
  if (quoted) {
    return std::unexpected("a quote is not closed");
  }
  if (in_word) {
    words.push_back(std::move(word));
  }
  return words;
}

CommandResultText ArgumentsJson(const CommandEntry& entry, std::span<const std::string> tokens) {
  std::vector<std::optional<std::string>> values(entry.arguments.size());
  std::size_t next_positional = 0;
  for (const std::string& token : tokens) {
    const std::size_t equals = token.find('=');
    const auto named = equals == std::string::npos
                           ? entry.arguments.end()
                           : std::ranges::find(entry.arguments, token.substr(0, equals), &CommandArgument::name);
    std::size_t index = 0;
    std::string value;
    if (named != entry.arguments.end()) {
      index = static_cast<std::size_t>(named - entry.arguments.begin());
      value = token.substr(equals + 1);
    } else {
      while (next_positional < values.size() && values[next_positional]) {
        ++next_positional;
      }
      if (next_positional == values.size()) {
        return std::unexpected(std::format("{} takes {} argument(s), got more", entry.name, values.size()));
      }
      index = next_positional;
      value = token;
    }
    const auto json = JsonValue(entry.arguments[index], value);
    if (!json) {
      return json;
    }
    values[index] = *json;
  }
  std::string object = "{";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (values[i]) {
      object += std::format("{}\"{}\":{}", object.size() > 1 ? "," : "", entry.arguments[i].name, *values[i]);
    }
  }
  return object + "}";
}

void EnsureCommands(flecs::world& world) {
  if (world.has<Commands>()) {
    return;
  }
  world.component<CommandRequest>();
  world.component<CommandPhase>().add(flecs::Phase).depends_on(flecs::PostLoad);
  world.component<CommandCollectPhase>().add(flecs::Phase).depends_on<CommandPhase>();
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
        std::optional<CommandResultText> reply = entry.collect(request);
        if (!reply && state.age++ == 0) {
          return;
        }
        CommandCallback callback = std::move(pending->second.callback);
        commands.pending.erase(pending);
        request.destruct();
        callback(reply ? std::move(*reply)
                       : CommandResultText(std::unexpected(std::format("nothing answered '{}'", entry.name))));
      });
}

Status SubmitCommand(flecs::world world, const CommandFilter& filter, std::string_view name, std::string_view json,
                     CommandCallback callback) {
  EnsureCommands(world);
  Commands& commands = world.get_mut<Commands>();
  const auto found = commands.entries.find(name);
  if (found == commands.entries.end() || !Shown(found->second, filter)) {
    return std::unexpected(std::format("unknown command '{}'", name));
  }
  if (found->second.handlers == 0) {
    return std::unexpected(std::format("no handler for '{}'", name));
  }
  const auto request = found->second.create(world, json);
  if (!request) {
    return std::unexpected(request.error());
  }
  const std::uint64_t ticket = commands.next_ticket++;
  request->set<CommandRequest>({.ticket = ticket});
  commands.pending.emplace(ticket, PendingCommand {.name = found->second.name, .callback = std::move(callback)});
  return {};
}

Status SubmitCommandLine(flecs::world world, const CommandFilter& filter, std::string_view line,
                         CommandCallback callback) {
  const auto words = SplitCommandLine(line);
  if (!words) {
    return std::unexpected(words.error());
  }
  if (words->empty()) {
    return std::unexpected("empty line");
  }
  EnsureCommands(world);
  const auto& entries = world.get<Commands>().entries;
  const auto found = entries.find(words->front());
  if (found == entries.end() || !Shown(found->second, filter)) {
    return std::unexpected(std::format("unknown command '{}'", words->front()));
  }
  const auto json = ArgumentsJson(found->second, std::span(*words).subspan(1));
  if (!json) {
    return std::unexpected(json.error());
  }
  return SubmitCommand(world, filter, words->front(), *json, std::move(callback));
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
