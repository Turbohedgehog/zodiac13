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

#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>
#include <raylib.h>

#include <lib_core/utils/asset_source.h>

namespace z13::raylib {

class ViewCulling;

// A prop of a station: a shared model at a pose.
struct PlacedProp {
  std::shared_ptr<::Model> model;
  ::Matrix transform {};
  // In meters, for culling.
  Eigen::AlignedBox3f bounds;
};

// The models standing in a station over its blocks (assets/station/decor/<scene>.json), drawn
// with the lighting shader. Render-only singleton, never state.
class StationProps {
 public:
  using Singleton = void;

  // Loads the props of `scene` and the models they name; `lighting_shader` is borrowed by every
  // material, id 0 keeps raylib's default. A scene without props yields none.
  static std::expected<StationProps, std::string> Load(
      const AssetSource& assets, std::string_view scene, ::Shader lighting_shader);

  // The props in view; returns how many it drew.
  size_t Draw(const ViewCulling& culling) const;

  size_t Count() const { return props_.size(); }

  // Palette primitives whose blocks are not drawn: these props stand in for them.
  const std::vector<std::string>& Replaced() const { return replaced_; }

 private:
  std::vector<PlacedProp> props_;
  std::vector<std::string> replaced_;
};

}  // namespace z13::raylib
