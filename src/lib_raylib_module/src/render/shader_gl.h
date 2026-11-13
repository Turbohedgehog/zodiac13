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

#include <string>

#include <lib_core/utils/status.h>

namespace z13::raylib {

// What the drivers say about a pair of stages: empty when both compile and link.
std::string ShaderBuildLog(const std::string& vertex, const std::string& fragment);

// Makes the linked program `target` a copy of `source`, keeping the id that materials copied.
z13::Status CopyProgram(unsigned int target, unsigned int source);

}  // namespace z13::raylib
