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

#include <lib_core/state/component_codec.h>
#include <lib_core/utils/status.h>

#include <algorithm>
#include <format>

#include "component_codec/meta_access.h"
#include "component_codec/scratch_value.h"
#include "component_codec/value_decoder.h"
#include "component_codec/value_encoder.h"

namespace z13::flecs_tools {

namespace {

using codec::Error;

Status CheckSize(const flecs::world& world, flecs::entity_t type, size_t size) {
  return codec::TypeSize(world, type).and_then([&](size_t expected_size) -> Status {
    if (size != expected_size) {
      return Error(std::format("{} bytes for '{}' of size {}", size, codec::TypeName(world, type), expected_size));
    }
    return {};
  });
}

}  // namespace

std::expected<std::vector<uint8_t>, std::string> EncodeValue(
    const flecs::world& world, flecs::entity_t type, std::span<const std::byte> value) {
  codec::ValueEncoder encoder(world);
  return CheckSize(world, type, value.size())
      .and_then([&] { return encoder.Write(type, value); })
      .transform([&] { return encoder.Take(); });
}

Status DecodeValue(
    const flecs::world& world, flecs::entity_t type, std::span<std::byte> value, std::span<const uint8_t> bytes) {
  codec::ValueDecoder decoder(world, bytes);
  return CheckSize(world, type, value.size())
      .and_then([&] { return decoder.Read(type, value); })
      .and_then([&]() -> Status {
        if (!decoder.AtEnd()) {
          return Error("trailing bytes after value");
        }
        return {};
      });
}

std::expected<std::span<std::byte>, std::string> ComponentBytes(
    const flecs::world& world, flecs::entity_t type, void* value) {
  return codec::ViewValue(world, type, value);
}

std::expected<std::span<const std::byte>, std::string> ComponentBytes(
    const flecs::world& world, flecs::entity_t type, const void* value) {
  return codec::ViewValue(world, type, value);
}

namespace {

// An Eigen type or std::array: an opaque array of scalars that fills the value's memory exactly.
bool IsFlatArrayOpaque(const flecs::world& world, flecs::entity_t type);

}  // namespace

bool IsFlatValue(const flecs::world& world, flecs::entity_t type) {
  const auto meta = codec::Meta<EcsType>(world, type);
  if (!meta) {
    return false;
  }
  switch (meta->get().kind) {
    case EcsPrimitiveType:
      return codec::Meta<EcsPrimitive>(world, type).transform([](const EcsPrimitive& primitive) {
        return primitive.kind != EcsString && primitive.kind != EcsEntity;
      }).value_or(false);
    case EcsEnumType:
    case EcsBitmaskType:
      return true;
    case EcsStructType: {
      const auto struct_meta = codec::Meta<EcsStruct>(world, type);
      if (!struct_meta) {
        return false;
      }
      const std::span members(
          static_cast<const ecs_member_t*>(ecs_vec_first(&struct_meta->get().members)),
          static_cast<size_t>(ecs_vec_count(&struct_meta->get().members)));
      return std::ranges::all_of(members, [&](const ecs_member_t& member) { return IsFlatValue(world, member.type); });
    }
    case EcsArrayType:
      return codec::Meta<EcsArray>(world, type).transform([&](const EcsArray& array) {
        return IsFlatValue(world, array.type);
      }).value_or(false);
    case EcsOpaqueType:
      return IsFlatArrayOpaque(world, type);
    default:
      return false;
  }
}

namespace {

void AppendRange(std::vector<ByteRange>& ranges, size_t offset, size_t size) {
  if (!ranges.empty() && ranges.back().offset + ranges.back().size == offset) {
    ranges.back().size += size;
  } else {
    ranges.push_back({.offset = offset, .size = size});
  }
}

void AppendFlatRanges(const flecs::world& world, flecs::entity_t type, size_t base, std::vector<ByteRange>& ranges) {
  const auto meta = codec::Meta<EcsType>(world, type);
  const auto size = codec::TypeSize(world, type);
  if (!meta || !size) {
    return;
  }
  const auto repeat = [&](flecs::entity_t element_type, size_t count, size_t first) {
    const size_t element_size = codec::TypeSize(world, element_type).value_or(0);
    for (size_t index = 0; index < count; ++index) {
      AppendFlatRanges(world, element_type, first + (index * element_size), ranges);
    }
  };
  switch (meta->get().kind) {
    case EcsStructType:
      if (const auto members = codec::Meta<EcsStruct>(world, type)) {
        const std::span all(static_cast<const ecs_member_t*>(ecs_vec_first(&members->get().members)),
                            static_cast<size_t>(ecs_vec_count(&members->get().members)));
        for (const ecs_member_t& member : all) {
          repeat(member.type, std::max<size_t>(static_cast<size_t>(member.count), 1),
                 base + static_cast<size_t>(member.offset));
        }
      }
      break;
    case EcsArrayType:
      if (const auto array = codec::Meta<EcsArray>(world, type)) {
        repeat(array->get().type, static_cast<size_t>(array->get().count), base);
      }
      break;
    default:
      AppendRange(ranges, base, *size);
      break;
  }
}

bool IsFlatArrayOpaque(const flecs::world& world, flecs::entity_t type) {
  const auto opaque = codec::Meta<EcsOpaque>(world, type);
  if (!opaque) {
    return false;
  }
  const auto as_type = codec::Meta<EcsType>(world, opaque->get().as_type);
  const auto array = codec::Meta<EcsArray>(world, opaque->get().as_type);
  if (!as_type || as_type->get().kind != EcsArrayType || !array || !IsFlatValue(world, array->get().type)) {
    return false;
  }
  const auto value_size = codec::TypeSize(world, type);
  const auto element_size = codec::TypeSize(world, array->get().type);
  return value_size && element_size && *value_size == *element_size * static_cast<size_t>(array->get().count);
}

}  // namespace

std::vector<ByteRange> FlatRanges(const flecs::world& world, flecs::entity_t type) {
  std::vector<ByteRange> ranges;
  AppendFlatRanges(world, type, 0, ranges);
  return ranges;
}

Status CheckEncodable(const flecs::world& world, flecs::entity_t type) {
  return codec::ScratchValue::Create(world, type).and_then([&](const codec::ScratchValue& scratch) {
    return EncodeValue(world, type, scratch.Bytes()).transform([](const std::vector<uint8_t>&) {});
  });
}

Status ValidateValue(
    const flecs::world& world, flecs::entity_t type, std::span<const uint8_t> bytes) {
  return codec::ScratchValue::Create(world, type).and_then([&](const codec::ScratchValue& scratch) {
    return DecodeValue(world, type, scratch.Bytes(), bytes);
  });
}

std::expected<std::string, std::string> ValueToJson(
    const flecs::world& world, flecs::entity_t type, std::span<const uint8_t> bytes) {
  const auto scratch = codec::ScratchValue::Create(world, type);
  if (!scratch) {
    return Error(scratch.error());
  }
  if (auto read = DecodeValue(world, type, scratch->Bytes(), bytes); !read) {
    return Error(read.error());
  }
  const flecs::string json = world.to_json(type, scratch->Bytes().data());
  if (json.size() == 0) {
    return Error(std::format("'{}' has no JSON serializer", codec::TypeName(world, type)));
  }
  return std::string(json.c_str());
}

std::expected<std::vector<uint8_t>, std::string> ValueFromJson(
    const flecs::world& world, flecs::entity_t type, std::string_view json) {
  const auto scratch = codec::ScratchValue::Create(world, type);
  if (!scratch) {
    return Error(scratch.error());
  }
  const std::string owned(json);
  flecs::world mutable_world = world;
  if (mutable_world.from_json(type, scratch->Bytes().data(), owned.c_str()) == nullptr) {
    return Error(std::format("invalid JSON value for '{}'", codec::TypeName(world, type)));
  }
  return EncodeValue(world, type, scratch->Bytes());
}

}  // namespace z13::flecs_tools
