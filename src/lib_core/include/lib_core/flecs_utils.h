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

#include <functional>
#include <optional>

#include <flecs.h>

#include <lib_core/config.h>

namespace z13 {

class WorldNoDeferGuard {
 public:
  explicit WorldNoDeferGuard(flecs::world world);
  ~WorldNoDeferGuard();

 private:
  flecs::world world_;
};

// Runs flecs operations immediately, even inside an observer, where deferred
// `.member()` calls overwrite each other (see CLAUDE.md). Nested instances are
// safe: only the outermost one actually suspends/resumes deferring.
class ImmediateScope {
 public:
  explicit ImmediateScope(flecs::world& world);
  ~ImmediateScope();

  ImmediateScope(const ImmediateScope&) = delete;
  ImmediateScope& operator=(const ImmediateScope&) = delete;

 private:
  flecs::world& world_;
  bool needs_resume_ {};
};

// Config of the Core that owns `world`; nullopt in worlds with no CoreComponent
// (e.g. bare lib_core tests).
std::optional<std::reference_wrapper<const Config>> GetCoreConfig(flecs::world world);

}  // namespace z13
