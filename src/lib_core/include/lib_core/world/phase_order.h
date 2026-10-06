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

#include <flecs.h>

#include <lib_core/utils/status.h>

namespace z13 {

// Makes the phase order total and build-independent: a topological sort of the declared
// DependsOn graph plus flecs' builtin order, ties broken by phase path (not entity id,
// which differs between builds). Call once every module has declared its phases.
//
// Like flecs' own builtin phases, each phase then depends only on its own hidden anchor
// (anchors form the chain), so disabling a phase stops just that phase's systems.
// Fails on a DependsOn cycle, leaving the order as declared.
Status LinearizePhases(flecs::world& world);

}  // namespace z13
