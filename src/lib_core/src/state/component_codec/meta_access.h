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

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>

#include <flecs.h>

#include <lib_core/utils/status.h>

namespace z13::flecs_tools::codec {

// Primitives are copied as-is: every supported platform is little-endian.
static_assert(std::endian::native == std::endian::little);

using Error = std::unexpected<std::string>;

// A value's memory, exactly its type's size.
using ValueBytes = std::span<std::byte>;
using ConstValueBytes = std::span<const std::byte>;

// Pointer-sized integers are always 8 bytes, independent of the platform.
using WideUPtr = uint64_t;
using WideIPtr = int64_t;

template <class T>
flecs::entity_t MetaId();
template <>
inline flecs::entity_t MetaId<EcsType>() { return ecs_id(EcsType); }
template <>
inline flecs::entity_t MetaId<EcsPrimitive>() { return ecs_id(EcsPrimitive); }
template <>
inline flecs::entity_t MetaId<EcsEnum>() { return ecs_id(EcsEnum); }
template <>
inline flecs::entity_t MetaId<EcsConstants>() { return ecs_id(EcsConstants); }
template <>
inline flecs::entity_t MetaId<EcsStruct>() { return ecs_id(EcsStruct); }
template <>
inline flecs::entity_t MetaId<EcsArray>() { return ecs_id(EcsArray); }
template <>
inline flecs::entity_t MetaId<EcsVector>() { return ecs_id(EcsVector); }
template <>
inline flecs::entity_t MetaId<EcsOpaque>() { return ecs_id(EcsOpaque); }

std::string TypeName(const flecs::world& world, flecs::entity_t type);

// One of the type's meta components (EcsType, EcsStruct, ...); an error if it has none.
template <class T>
std::expected<std::reference_wrapper<const T>, std::string> Meta(const flecs::world& world, flecs::entity_t type) {
  const auto* meta = static_cast<const T*>(ecs_get_id(world.c_ptr(), type, MetaId<T>()));
  if (meta == nullptr) {
    return Error(std::format("'{}' lacks meta", TypeName(world, type)));
  }
  return std::cref(*meta);
}

std::expected<size_t, std::string> TypeSize(const flecs::world& world, flecs::entity_t type);

// Byte size of a fixed-size primitive; nullopt for strings, entities, ids and pointer-sized ints.
std::optional<size_t> FixedSize(ecs_primitive_kind_t kind);

// An integer primitive's value, widened.
std::optional<int64_t> AsInt64(ecs_primitive_kind_t kind, ConstValueBytes value);

// Views flecs-owned memory as a value of `type`; an error if it is null.
std::expected<ValueBytes, std::string> ViewValue(const flecs::world& world, flecs::entity_t type, void* value);
std::expected<ConstValueBytes, std::string> ViewValue(
    const flecs::world& world, flecs::entity_t type, const void* value);

// `size` bytes at `offset`, if they fit.
std::expected<ValueBytes, std::string> Slice(ValueBytes value, size_t offset, size_t size);
std::expected<ConstValueBytes, std::string> Slice(ConstValueBytes value, size_t offset, size_t size);

template <class T>
std::expected<T, std::string> Load(ConstValueBytes value) {
  if (value.size() < sizeof(T)) {
    return Error("value smaller than its primitive");
  }
  T loaded {};
  std::memcpy(&loaded, value.data(), sizeof(T));
  return loaded;
}

template <class T>
Status Store(ValueBytes value, const T& stored) {
  if (value.size() < sizeof(T)) {
    return Error("value smaller than its primitive");
  }
  std::memcpy(value.data(), &stored, sizeof(T));
  return {};
}

}  // namespace z13::flecs_tools::codec
