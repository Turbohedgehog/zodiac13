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

#include <memory>

#include <raylib.h>

#include <z13_primitives/geometry.h>

namespace z13::raylib {

// Models of placed primitives, one per (type, size) shared by every block of that shape,
// each with its checker texture. Singleton exempt from the "no pointers" rule (see
// CLAUDE.md); never state.
class BlockMeshes {
 public:
  using Singleton = void;
  using OptionalPalette = z13::building::primitives::OptionalPalette;

  // `lighting_shader` is borrowed by every model's material; id 0 keeps raylib's default.
  explicit BlockMeshes(::Shader lighting_shader = {});

  // Built on first use; a type the palette lacks gets a grey box.
  std::shared_ptr<::Model> Get(const z13::building::primitives::BlockShapeKey& shape, OptionalPalette palette);

  // Frees the models no Get() asked for since the previous call.
  void ReleaseUnused();

 private:
  class State;

  std::shared_ptr<State> state_;
};

}  // namespace z13::raylib
