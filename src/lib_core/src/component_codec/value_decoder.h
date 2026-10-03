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
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>

#include <flecs.h>

#include "meta_access.h"

namespace z13::flecs_tools::codec {

// Reads untrusted bytes: every length, count and constant is checked.
class ValueDecoder {
 public:
  ValueDecoder(const flecs::world& world, std::span<const uint8_t> bytes) : world_(world), bytes_(bytes) {}

  Status Read(flecs::entity_t type, ValueBytes value);
  bool AtEnd() const { return bytes_.empty(); }

 private:
  const flecs::world& World() const { return world_.get(); }

  Status ReadInto(ValueBytes dst);
  template <class T>
  std::expected<T, std::string> ReadScalar() {
    T scalar {};
    return ReadInto(std::as_writable_bytes(std::span(&scalar, 1))).transform([&scalar] { return scalar; });
  }
  std::expected<size_t, std::string> ReadCount();
  std::expected<std::string, std::string> ReadString();
  std::expected<flecs::entity_t, std::string> ReadEntity();

  Status ReadPrimitive(flecs::entity_t type, ValueBytes value);
  Status ReadEnum(flecs::entity_t type, ValueBytes value);
  Status ReadBitmask(flecs::entity_t type, ValueBytes value);
  Status ReadStruct(flecs::entity_t type, ValueBytes value);
  Status ReadElements(flecs::entity_t type, size_t count, ValueBytes values);
  Status ReadOpaque(flecs::entity_t type, ValueBytes value);
  Status ReadOpaqueElements(const EcsOpaque& opaque, ecs_type_kind_t as_kind, ValueBytes value);
  Status AssignOpaquePrimitive(const EcsOpaque& opaque, ecs_primitive_kind_t kind, ValueBytes value);

  // Enum and bitmask constants share a layout (flecs meta.h).
  template <class Constant>
  std::span<const Constant> Constants(flecs::entity_t type) const {
    const auto constants = Meta<EcsConstants>(World(), type);
    if (!constants) {
      return {};
    }
    const ecs_vec_t& ordered = constants->get().ordered_constants;
    return {static_cast<const Constant*>(ecs_vec_first(&ordered)), static_cast<size_t>(ecs_vec_count(&ordered))};
  }

  std::reference_wrapper<const flecs::world> world_;
  std::span<const uint8_t> bytes_ {};
};

}  // namespace z13::flecs_tools::codec
