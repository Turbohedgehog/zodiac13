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

namespace z13::gameplay {

// Name of the entity GameplaySystem::CreateTestPlayer creates on world
// startup. Single source of truth shared by that creation code and by
// anything (production or test) that needs to look the entity up by name.
constexpr std::string_view kTestPlayerEntityName = "TestPlayer";

}  // namespace z13::gameplay
