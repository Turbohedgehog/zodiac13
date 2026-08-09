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

#include <optional>

#include <lib_core/core_types.h>

namespace z13 {

struct PendingDestroy {};

struct CoreComponent {
  std::optional<CoreRef> core;
};

struct RegisterComponentsEvent {};
struct InitPhasesEvent {};
struct InitSystemsEvent {};
struct InitWorldDataEvent {};

}  // namespace z13
