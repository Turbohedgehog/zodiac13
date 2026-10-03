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

#include "meta_access.h"

namespace z13::flecs_tools::codec {

namespace {

template <class T>
std::optional<int64_t> Widen(ConstValueBytes value) {
  const auto loaded = Load<T>(value);
  return loaded ? std::optional<int64_t>(static_cast<int64_t>(*loaded)) : std::nullopt;
}

template <class Bytes, class Pointer>
std::expected<Bytes, std::string> View(const flecs::world& world, flecs::entity_t type, Pointer value) {
  if (value == nullptr) {
    return Error(std::format("null value of '{}'", TypeName(world, type)));
  }
  return TypeSize(world, type).transform([value](size_t size) {
    return Bytes(static_cast<typename Bytes::pointer>(value), size);
  });
}

template <class Bytes>
std::expected<Bytes, std::string> SliceOf(Bytes value, size_t offset, size_t size) {
  if (offset > value.size() || size > value.size() - offset) {
    return Error("member outside its value");
  }
  return value.subspan(offset, size);
}

}  // namespace

std::string TypeName(const flecs::world& world, flecs::entity_t type) {
  return flecs::entity(world.c_ptr(), type).path().c_str();
}

std::expected<size_t, std::string> TypeSize(const flecs::world& world, flecs::entity_t type) {
  const ecs_type_info_t* info = ecs_get_type_info(world.c_ptr(), type);
  if (info == nullptr) {
    return Error(std::format("'{}' has no type info", TypeName(world, type)));
  }
  return static_cast<size_t>(info->size);
}

std::optional<size_t> FixedSize(ecs_primitive_kind_t kind) {
  switch (kind) {
    case EcsBool:
      [[fallthrough]];
    case EcsChar:
      [[fallthrough]];
    case EcsByte:
      [[fallthrough]];
    case EcsU8:
      [[fallthrough]];
    case EcsI8:
      return 1;
    case EcsU16:
      [[fallthrough]];
    case EcsI16:
      return 2;
    case EcsU32:
      [[fallthrough]];
    case EcsI32:
      [[fallthrough]];
    case EcsF32:
      return 4;
    case EcsU64:
      [[fallthrough]];
    case EcsI64:
      [[fallthrough]];
    case EcsF64:
      return 8;
    default:
      return std::nullopt;
  }
}

std::optional<int64_t> AsInt64(ecs_primitive_kind_t kind, ConstValueBytes value) {
  switch (kind) {
    case EcsU8:
      return Widen<uint8_t>(value);
    case EcsU16:
      return Widen<uint16_t>(value);
    case EcsU32:
      return Widen<uint32_t>(value);
    case EcsU64:
      return Widen<uint64_t>(value);
    case EcsI8:
      return Widen<int8_t>(value);
    case EcsI16:
      return Widen<int16_t>(value);
    case EcsI32:
      return Widen<int32_t>(value);
    case EcsI64:
      return Widen<int64_t>(value);
    default:
      return std::nullopt;
  }
}

std::expected<ValueBytes, std::string> ViewValue(const flecs::world& world, flecs::entity_t type, void* value) {
  return View<ValueBytes>(world, type, static_cast<std::byte*>(value));
}

std::expected<ConstValueBytes, std::string> ViewValue(
    const flecs::world& world, flecs::entity_t type, const void* value) {
  return View<ConstValueBytes>(world, type, static_cast<const std::byte*>(value));
}

std::expected<ValueBytes, std::string> Slice(ValueBytes value, size_t offset, size_t size) {
  return SliceOf(value, offset, size);
}

std::expected<ConstValueBytes, std::string> Slice(ConstValueBytes value, size_t offset, size_t size) {
  return SliceOf(value, offset, size);
}

}  // namespace z13::flecs_tools::codec
