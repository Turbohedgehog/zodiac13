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
#include <unordered_map>

#include <raylib.h>

#include <z13_primitives/geometry.h>

#include "block_meshes.h"

namespace z13::raylib {

struct CachedBlockModel {
  std::shared_ptr<::Model> model;
  bool used {};
};

class BlockMeshes::State {
 public:
  explicit State(::Shader lighting_shader) : lighting_shader_(lighting_shader) {}

  std::shared_ptr<::Model> Get(const z13::building::primitives::BlockShapeKey& shape, OptionalPalette palette);
  void ReleaseUnused();

 private:
  std::shared_ptr<::Model> Build(const z13::building::primitives::BlockShapeKey& shape, OptionalPalette palette) const;

  ::Shader lighting_shader_ {};
  std::unordered_map<z13::building::primitives::BlockShapeKey, CachedBlockModel,
                     z13::building::primitives::BlockShapeKeyHash>
      models_;
};

}  // namespace z13::raylib
