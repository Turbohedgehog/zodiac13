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
#include <vector>

#include <primitives/placement.h>

#include <cell_light/lamp.h>

namespace z13::station::light {

// A pane of glass: opaque to the nodes, so its two sides keep their own light, with `transmission`
// of a lamp's light crossing it (Primitive::light_transmission).
struct GlassBox {
  z13::building::primitives::CellBox cells;
  float transmission = 1.f;

  bool operator==(const GlassBox&) const = default;
};

// What changed since the light was flooded. Opaque blocks don't overlap (ValidateBuild),
// so a removed box frees its cells.
struct LightEdits {
  std::vector<z13::building::primitives::CellBox> opaque_added;
  std::vector<z13::building::primitives::CellBox> opaque_removed;
  std::vector<GlassBox> glass_added;
  std::vector<GlassBox> glass_removed;
  std::vector<Lamp> lamps_added;
  std::vector<Lamp> lamps_removed;

  bool Empty() const;
};

// What an update changed, for the renderer to upload: labels in cells, light in cubes from
// the layout's origin.
struct LightChanges {
  // The layout itself changed: everything did.
  bool resized {};
  std::optional<z13::building::primitives::CellBox> labels;
  std::optional<z13::building::primitives::CellBox> light;
};

}  // namespace z13::station::light
