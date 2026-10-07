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

#include <raylib.h>

#include <z13_primitives/palette.h>

namespace z13::raylib {

struct Lighting;

// Locations of the checker uniforms in assets/shaders/lighting.fs.
struct CheckerUniforms {
  int enabled {};
  int first {};
  int second {};
};

// Also sets the cell size the checker squares follow.
CheckerUniforms SetupCheckerUniforms(const ::Shader& shader);

// Until StopChecker, what is drawn takes `material`'s squares from its world position instead
// of its texture. Both do nothing without a lighting shader.
void UseChecker(const Lighting& lighting, const z13::building::primitives::Checker& material);
void StopChecker(const Lighting& lighting);

}  // namespace z13::raylib
