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
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <primitives_generated.h>

namespace z13::primitives {

using PrimitiveFlags = fbs::station::PrimitiveFlags;

enum class ShapeKind : uint8_t { kBox, kWedge, kCornerWedge, kDoorFrame };

struct Shape {
  ShapeKind kind {};
  // DoorFrame only: the opening's width (X) and height (Z), in cells.
  Eigen::Vector2i door_opening = Eigen::Vector2i::Zero();
};

using Rgba = std::array<uint8_t, 4>;

struct Checker {
  Rgba first {};
  Rgba second {};
};

// One palette entry, as plain data so a flecs component can hold it.
struct Primitive {
  uint32_t id {};
  std::string name;
  Shape shape;
  Eigen::Vector3i min_size = Eigen::Vector3i::Ones();
  Eigen::Vector3i max_size = Eigen::Vector3i::Ones();
  Checker material;
  PrimitiveFlags flags {};

  bool Has(PrimitiveFlags flag) const { return (flags & flag) == flag; }
};

struct Palette {
  std::vector<Primitive> primitives;
  // Of the palette's content, not its JSON text; peers compare it to agree on one palette.
  uint64_t hash {};

  std::optional<std::reference_wrapper<const Primitive>> Find(uint32_t id) const;
};

// The loaded palette; rebuilt from the file at startup, never state.
struct BlockPalette {
  using Singleton = void;
  Palette palette;
};

// Largest extent of a primitive along any axis (64 m at 0.25 m cells).
inline constexpr int kMaxSizeCells = 256;

// Parses palette JSON (primitives.fbs) and rejects duplicate ids or names, size limits
// outside 1..kMaxSizeCells or min above max, stretchable door frames, and shapes that
// can't be built at those sizes.
std::expected<Palette, std::string> ParsePalette(std::string_view json);

}  // namespace z13::primitives
