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

#include "value_encoder.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>

namespace z13::flecs_tools::codec {

Status ValueEncoder::Write(flecs::entity_t type, ConstValueBytes value) {
  const auto meta = Meta<EcsType>(World(), type);
  if (!meta) {
    return Error(meta.error());
  }
  switch (meta->get().kind) {
    case EcsPrimitiveType:
      return WritePrimitive(type, value);
    case EcsEnumType:
      return Meta<EcsEnum>(World(), type).and_then([&](const EcsEnum& enum_meta) {
        return Write(enum_meta.underlying_type, value);
      });
    case EcsBitmaskType:
      return Load<uint32_t>(value).transform([this](uint32_t bits) { AppendScalar(bits); });
    case EcsStructType:
      return WriteStruct(type, value);
    case EcsArrayType:
      return Meta<EcsArray>(World(), type).and_then([&](const EcsArray& array) {
        return WriteElements(array.type, static_cast<size_t>(array.count), value);
      });
    case EcsOpaqueType:
      return WriteOpaque(type, value);
    default:
      return Error(std::format("'{}' has an unsupported meta kind", TypeName(World(), type)));
  }
}

int ValueEncoder::OnOpaqueValue(const ecs_serializer_t* serializer, ecs_entity_t type, const void* value) {
  auto& sink = *static_cast<OpaqueSink*>(serializer->ctx);
  ValueEncoder& encoder = sink.encoder;
  ++sink.count;
  sink.status = ViewValue(encoder.World(), type, value).and_then([&](ConstValueBytes bytes) {
    return encoder.Write(type, bytes);
  });
  return sink.status ? 0 : -1;
}

int ValueEncoder::OnOpaqueMember(const ecs_serializer_t* serializer, const char*) {
  static_cast<OpaqueSink*>(serializer->ctx)->status = Error("opaque structs are not supported");
  return -1;
}

Status ValueEncoder::WritePrimitive(flecs::entity_t type, ConstValueBytes value) {
  const auto primitive = Meta<EcsPrimitive>(World(), type);
  if (!primitive) {
    return Error(primitive.error());
  }
  switch (const ecs_primitive_kind_t kind = primitive->get().kind) {
    case EcsString:
      return Load<const char*>(value).transform([this](const char* str) {
        AppendString(str == nullptr ? std::string_view {} : std::string_view {str});
      });
    case EcsEntity:
      return Load<flecs::entity_t>(value).transform([this](flecs::entity_t entity) {
        AppendString(entity == 0 ? std::string {} : TypeName(World(), entity));
      });
    case EcsUPtr:
      return Load<uintptr_t>(value).transform([this](uintptr_t scalar) { AppendScalar(WideUPtr {scalar}); });
    case EcsIPtr:
      return Load<intptr_t>(value).transform([this](intptr_t scalar) { AppendScalar(WideIPtr {scalar}); });
    default: {
      const std::optional<size_t> size = FixedSize(kind);
      if (!size) {
        return Error(std::format("primitive '{}' is not supported", TypeName(World(), type)));
      }
      return Slice(value, 0, *size).transform([this](ConstValueBytes scalar) { Append(scalar); });
    }
  }
}

Status ValueEncoder::WriteStruct(flecs::entity_t type, ConstValueBytes value) {
  const auto meta = Meta<EcsStruct>(World(), type);
  if (!meta) {
    return Error(meta.error());
  }
  const EcsStruct& struct_meta = *meta;
  const std::span members(
      static_cast<const ecs_member_t*>(ecs_vec_first(&struct_meta.members)),
      static_cast<size_t>(ecs_vec_count(&struct_meta.members)));
  for (const ecs_member_t& member : members) {
    const auto count = static_cast<size_t>(std::max(member.count, 1));
    const auto written = TypeSize(World(), member.type)
                             .and_then([&](size_t size) { return Slice(value, member.offset, size * count); })
                             .and_then([&](ConstValueBytes field) { return WriteElements(member.type, count, field); });
    if (!written) {
      return written;
    }
  }
  return {};
}

Status ValueEncoder::WriteElements(flecs::entity_t type, size_t count, ConstValueBytes values) {
  const auto size = TypeSize(World(), type);
  if (!size) {
    return Error(size.error());
  }
  for (size_t i = 0; i < count; ++i) {
    const auto written =
        Slice(values, i * *size, *size).and_then([&](ConstValueBytes element) { return Write(type, element); });
    if (!written) {
      return written;
    }
  }
  return {};
}

// Written as `as_type`; a collection with its element count first.
Status ValueEncoder::WriteOpaque(flecs::entity_t type, ConstValueBytes value) {
  const auto opaque = Meta<EcsOpaque>(World(), type);
  if (!opaque || opaque->get().serialize == nullptr) {
    return Error(std::format("opaque '{}' has no serializer", TypeName(World(), type)));
  }
  const auto as_type = Meta<EcsType>(World(), opaque->get().as_type);
  if (!as_type) {
    return Error(as_type.error());
  }

  ValueEncoder elements(World());
  OpaqueSink sink {.encoder = elements};
  ecs_serializer_t serializer {};
  serializer.value_ = &OnOpaqueValue;
  serializer.member_ = &OnOpaqueMember;
  serializer.world = World().c_ptr();
  serializer.ctx = &sink;
  if (opaque->get().serialize(&serializer, value.data()) != 0 || !sink.status) {
    return Error(std::format("opaque '{}': {}", TypeName(World(), type),
                             sink.status ? "serialize failed" : sink.status.error()));
  }

  const ecs_type_kind_t kind = as_type->get().kind;
  if (kind == EcsArrayType || kind == EcsVectorType) {
    AppendCount(sink.count);
  } else if (kind != EcsPrimitiveType || sink.count != 1) {
    return Error(std::format("opaque '{}' is not a primitive or collection", TypeName(World(), type)));
  }
  const std::vector<uint8_t> bytes = elements.Take();
  bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  return {};
}

void ValueEncoder::Append(std::span<const std::byte> data) {
  std::ranges::transform(data, std::back_inserter(bytes_), [](std::byte byte) { return std::to_integer<uint8_t>(byte); });
}

void ValueEncoder::AppendString(std::string_view str) {
  AppendCount(str.size());
  Append(std::as_bytes(std::span(str)));
}

}  // namespace z13::flecs_tools::codec
