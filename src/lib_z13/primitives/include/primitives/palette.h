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

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/color.h>

// primitives.fbs's flag sets, declared here so this header stays clear of primitives_generated.h:
// code naming a flag or combining them includes that.
namespace z13::fbs::station {
enum class PrimitiveFlags : uint32_t;
enum class InteractionMethods : uint8_t;
struct Rgba;
}  // namespace z13::fbs::station

namespace z13::building::primitives {

using PrimitiveFlags = fbs::station::PrimitiveFlags;
using InteractionMethods = fbs::station::InteractionMethods;

// Temporary: a fixed set of shapes, to be replaced by data/models in a later stage.
enum class ShapeKind : uint8_t { kBox, kWedge, kCornerWedge, kDoorFrame, kPanel };

struct Shape {
  ShapeKind kind {};
  // DoorFrame only: the opening's width (X) and height (Z), in cells.
  Eigen::Vector2i door_opening = Eigen::Vector2i::Zero();
  // Wedge only: the run (X) is from one to this many times the rise (Z).
  int max_run_per_rise = 1;

  // `slant` is a panel's rise; every other shape takes none. A panel's vertical thickness
  // (size.z - slant) must seal against air at 6-connectivity: thickness * run >= run + slant.
  bool AllowsSize(const Eigen::Vector3i& size, int slant = 0) const;
  // The nearest allowed size: a wedge's run is brought into [rise, max_run_per_rise * rise].
  Eigen::Vector3i FitSize(Eigen::Vector3i size) const;
};

using Rgba = z13::Rgba;

struct Checker {
  Rgba first {};
  Rgba second {};
  bool emissive {};
  std::optional<std::string> texture;

  bool operator==(const Checker&) const = default;
};

// A point light just below the primitive's bottom face (primitives/lights.h).
struct LightSource {
  Rgba color {};
  int radius_cells {};
  float intensity {};

  bool operator==(const LightSource&) const = default;
};

// primitives.fbs's Interaction.
struct Interaction {
  InteractionMethods methods {};
  int sensor_cells {};

  bool Accepts(InteractionMethods method) const;
  bool operator==(const Interaction&) const = default;
};

// primitives.fbs's Port.
struct PortValue {
  std::string name;
  int64_t value {};

  bool operator==(const PortValue&) const = default;
};

struct PortSpec {
  std::string network;
  std::string role;
  std::string priority;
  std::vector<PortValue> values;

  // The value named `name`, if the port has one.
  std::optional<int64_t> Value(std::string_view name) const;
  bool operator==(const PortSpec&) const = default;
};

// One palette entry, as plain data so a flecs component can hold it.
struct Primitive {
  uint32_t id {};
  std::string name;
  std::string short_name;
  Shape shape;
  Eigen::Vector3i min_size = Eigen::Vector3i::Ones();
  Eigen::Vector3i max_size = Eigen::Vector3i::Ones();
  Checker material;
  PrimitiveFlags flags {};
  std::optional<LightSource> light;
  float light_transmission = 1.f;
  std::optional<Interaction> interaction;
  std::vector<PortSpec> ports;

  bool Has(PrimitiveFlags flag) const;
};

struct Palette {
  int max_size_cells {};
  std::vector<Primitive> primitives;
  // Of the palette's content, not its JSON text; peers compare it to agree on one palette.
  uint64_t hash {};

  std::optional<std::reference_wrapper<const Primitive>> Find(uint32_t id) const;
  std::optional<std::reference_wrapper<const Primitive>> Find(std::string_view name) const;
};

// The loaded palette; rebuilt from the file at startup, never state.
struct BlockPalette {
  using Singleton = void;
  Palette palette;
};

// A module's primitives in palette JSON; `file` names it in errors.
struct PaletteFragment {
  std::string file;
  std::string json;
};

// Parses palette JSON (primitives.fbs) and rejects duplicate ids or names, size limits
// outside 1..max_size_cells or min above max, stretchable door frames, shapes that can't
// be built at those sizes, lights without color, reach or intensity, interactions without a
// method or with a sensor that only presence has, and ports without a network or role, with
// a nameless, repeated or negative value, or two of one network. The fragments' primitives follow
// the base's, in the given order (callers sort them, so peers build one palette); only the base's
// max_size_cells counts.
std::expected<Palette, std::string> ParsePalette(std::string_view json, std::span<const PaletteFragment> fragments = {});

Rgba ToRgba(const fbs::station::Rgba& color);

}  // namespace z13::building::primitives
