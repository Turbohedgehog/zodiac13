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

#include <string_view>

#include <flecs.h>

#include <lib_core/commands/command.h>
#include <lib_core/commands/command_registry.h>

namespace z13::console_input {

// The tag of commands that only make sense in the console (help, clear); the console shows these and the debug ones.
inline constexpr FixedText kConsoleTag("console");

CommandFilter ConsoleFilter();

// What the console does with a line the player typed: logs it, files the command it names, and logs the reply
// (an error as an error) once the handlers ran. An empty line does nothing.
void RunConsoleLine(flecs::world world, std::string_view line);

}  // namespace z13::console_input
