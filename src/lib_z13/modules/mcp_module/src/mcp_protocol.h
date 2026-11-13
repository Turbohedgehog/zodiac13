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
#include <string>
#include <string_view>

#include <rfl.hpp>

#include <lib_core/commands/command_registry.h>

namespace z13::mcp {

// One request of the line protocol: a JSON object per line, {"id": 1, "command": "ping", "arguments": {}}.
struct McpRequest {
  std::int64_t id {};
  std::string command;
  rfl::Generic arguments;  // an object by field name
};

std::expected<McpRequest, std::string> ParseRequest(std::string_view line);

// A line the server sends on its own to the clients that subscribed: {"event": "say", "text": "..."}.
std::string FormatEvent(std::string_view event, std::string_view text);

// The reply line: {"id": 1, "ok": true, "result": ...} or {"id": 1, "ok": false, "error": "..."}.
std::string FormatReply(std::int64_t id, const CommandResult& result);

}  // namespace z13::mcp
