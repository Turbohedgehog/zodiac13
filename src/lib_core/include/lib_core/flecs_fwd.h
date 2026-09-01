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

// Minimal forward declarations of the flecs types used only by-reference in
// interfaces. Keeps the huge, non-modular <flecs.h> out of module BMIs and out
// of classic headers, while letting these stay single global-module entities
// shared with translation units that do include <flecs.h>. Safe in a module
// global-module fragment (a plain `namespace flecs {}` there is not).

namespace flecs {

struct world;
struct entity;

}  // namespace flecs
