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

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>
#include <rfl.hpp>

#include <lib_core/commands/command_field.h>
#include <lib_core/commands/command_registry.h>
#include <lib_core/utils/status.h>

namespace z13::console_input {

// Words with "double quotes" and backslash escapes.
std::expected<std::vector<std::string>, std::string> SplitCommandLine(std::string_view line);

// The arguments object of a command from its words, each positional or name=value, valued by the field's kind.
std::expected<rfl::Generic, std::string> ArgumentsOf(std::string_view command, std::span<const CommandField> fields,
                                                     std::span<const std::string> words);

// A reply as the console shows it: text as it is, anything else as JSON.
std::string ReplyText(const rfl::Generic& reply);

Status SubmitCommandLine(flecs::world world, const CommandFilter& filter, std::string_view line,
                         CommandCallback callback);

}  // namespace z13::console_input
