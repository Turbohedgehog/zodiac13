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


#include "mcp_protocol.h"

#include <optional>

#include <rfl/json.hpp>

namespace z13::mcp {

namespace {

struct RequestJson {
  std::int64_t id {};
  std::string command;
  std::optional<rfl::Generic> arguments;
};

struct ReplyJson {
  std::int64_t id {};
  bool ok {};
  std::optional<rfl::Generic> result;
  std::optional<std::string> error;
};

struct EventJson {
  std::string event;
  std::string text;
};

}  // namespace

std::expected<McpRequest, std::string> ParseRequest(std::string_view line) {
  const auto parsed = rfl::json::read<RequestJson, rfl::DefaultIfMissing>(std::string(line));
  if (!parsed) {
    return std::unexpected(std::string(parsed.error().what()));
  }
  if (parsed->command.empty()) {
    return std::unexpected("no command");
  }
  return McpRequest {.id = parsed->id,
                       .command = parsed->command,
                       .arguments = parsed->arguments.value_or(rfl::Generic(rfl::Generic::Object {}))};
}

std::string FormatEvent(std::string_view event, std::string_view text) {
  return rfl::json::write(EventJson {.event = std::string(event), .text = std::string(text)});
}

std::string FormatReply(std::int64_t id, const CommandResult& result) {
  ReplyJson reply {.id = id, .ok = result.has_value()};
  if (result) {
    reply.result = *result;
  } else {
    reply.error = result.error();
  }
  return rfl::json::write(reply);
}

}  // namespace z13::mcp
