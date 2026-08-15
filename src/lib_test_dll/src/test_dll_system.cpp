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

module;

#include <flecs.h>
#include <lib_core/log.h>

module z13.test_dll;

import z13.core;

namespace z13::dll {

void TestDllSystem::Register(flecs::world& world) {
  LOG_INFO("~~~~~ TestDllSystem::Register");
}

}  // namespace z13::dll