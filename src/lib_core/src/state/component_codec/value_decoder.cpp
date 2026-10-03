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

#include "value_decoder.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace z13::flecs_tools::codec {

Status ValueDecoder::Read(flecs::entity_t type, ValueBytes value) {
  const auto meta = Meta<EcsType>(World(), type);
  if (!meta) {
    return Error(meta.error());
  }
  switch (meta->get().kind) {
    case EcsPrimitiveType:
      return ReadPrimitive(type, value);
    case EcsEnumType:
      return ReadEnum(type, value);
    case EcsBitmaskType:
      return ReadBitmask(type, value);
    case EcsStructType:
      return ReadStruct(type, value);
    case EcsArrayType:
      return Meta<EcsArray>(World(), type).and_then([&](const EcsArray& array) {
        return ReadElements(array.type, static_cast<size_t>(array.count), value);
      });
    case EcsOpaqueType:
      return ReadOpaque(type, value);
    default:
      return Error(std::format("'{}' has an unsupported meta kind", TypeName(World(), type)));
  }
}

Status ValueDecoder::ReadInto(ValueBytes dst) {
  if (bytes_.size() < dst.size()) {
    return Error("truncated value");
  }
  std::ranges::transform(bytes_.first(dst.size()), dst.begin(), [](uint8_t byte) { return std::byte {byte}; });
  bytes_ = bytes_.subspan(dst.size());
  return {};
}

// Every element takes at least one byte.
std::expected<size_t, std::string> ValueDecoder::ReadCount() {
  return ReadScalar<uint32_t>().and_then([this](uint32_t count) -> std::expected<size_t, std::string> {
    if (count > bytes_.size()) {
      return Error("count exceeds the remaining bytes");
    }
    return count;
  });
}

std::expected<std::string, std::string> ValueDecoder::ReadString() {
  return ReadCount().transform([this](size_t size) {
    std::string str(size, '\0');
    std::ranges::transform(bytes_.first(size), str.begin(), [](uint8_t byte) { return static_cast<char>(byte); });
    bytes_ = bytes_.subspan(size);
    return str;
  });
}

std::expected<flecs::entity_t, std::string> ValueDecoder::ReadEntity() {
  return ReadString().and_then([this](const std::string& path) -> std::expected<flecs::entity_t, std::string> {
    if (path.empty()) {
      return flecs::entity_t {};
    }
    const flecs::entity entity = World().lookup(path.c_str());
    if (!entity) {
      return Error(std::format("unknown entity '{}'", path));
    }
    return entity.id();
  });
}

Status ValueDecoder::ReadPrimitive(flecs::entity_t type, ValueBytes value) {
  const auto primitive = Meta<EcsPrimitive>(World(), type);
  if (!primitive) {
    return Error(primitive.error());
  }
  switch (const ecs_primitive_kind_t kind = primitive->get().kind) {
    case EcsBool:
      return ReadScalar<uint8_t>().and_then([&](uint8_t byte) { return Store(value, byte != 0); });
    case EcsString: {
      const auto str = ReadString();
      const auto old = Load<char*>(value);
      if (!str || !old) {
        return Error(str ? old.error() : str.error());
      }
      ecs_os_free(*old);
      return Store(value, ecs_os_strdup(str->c_str()));
    }
    case EcsEntity:
      return ReadEntity().and_then([&](flecs::entity_t entity) { return Store(value, entity); });
    case EcsUPtr:
      return ReadScalar<WideUPtr>().and_then([&](WideUPtr wide) { return Store(value, static_cast<uintptr_t>(wide)); });
    case EcsIPtr:
      return ReadScalar<WideIPtr>().and_then([&](WideIPtr wide) { return Store(value, static_cast<intptr_t>(wide)); });
    default: {
      const std::optional<size_t> size = FixedSize(kind);
      if (!size) {
        return Error(std::format("primitive '{}' is not supported", TypeName(World(), type)));
      }
      return Slice(value, 0, *size).and_then([this](ValueBytes scalar) { return ReadInto(scalar); });
    }
  }
}

Status ValueDecoder::ReadEnum(flecs::entity_t type, ValueBytes value) {
  const auto enum_meta = Meta<EcsEnum>(World(), type);
  if (!enum_meta) {
    return Error(enum_meta.error());
  }
  const flecs::entity_t underlying = enum_meta->get().underlying_type;
  const auto primitive = Meta<EcsPrimitive>(World(), underlying);
  if (!primitive) {
    return Error(primitive.error());
  }
  if (auto read = Read(underlying, value); !read) {
    return read;
  }
  const std::optional<int64_t> decoded = AsInt64(primitive->get().kind, value);
  const bool known = decoded && std::ranges::any_of(Constants<ecs_enum_constant_t>(type), [&](const auto& constant) {
    return constant.value == *decoded || static_cast<int64_t>(constant.value_unsigned) == *decoded;
  });
  return known ? Status {} : Error(std::format("not a constant of enum '{}'", TypeName(World(), type)));
}

Status ValueDecoder::ReadBitmask(flecs::entity_t type, ValueBytes value) {
  const auto bits = ReadScalar<uint32_t>();
  if (!bits) {
    return Error(bits.error());
  }
  ecs_flags64_t known {};
  for (const ecs_bitmask_constant_t& constant : Constants<ecs_bitmask_constant_t>(type)) {
    known |= constant.value;
  }
  if ((*bits & ~known) != 0) {
    return Error(std::format("unknown flags in bitmask '{}'", TypeName(World(), type)));
  }
  return Store(value, *bits);
}

Status ValueDecoder::ReadStruct(flecs::entity_t type, ValueBytes value) {
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
    const auto read = TypeSize(World(), member.type)
                          .and_then([&](size_t size) { return Slice(value, member.offset, size * count); })
                          .and_then([&](ValueBytes field) { return ReadElements(member.type, count, field); });
    if (!read) {
      return read;
    }
  }
  return {};
}

Status ValueDecoder::ReadElements(flecs::entity_t type, size_t count, ValueBytes values) {
  const auto size = TypeSize(World(), type);
  if (!size) {
    return Error(size.error());
  }
  for (size_t i = 0; i < count; ++i) {
    const auto read = Slice(values, i * *size, *size).and_then([&](ValueBytes element) { return Read(type, element); });
    if (!read) {
      return read;
    }
  }
  return {};
}

Status ValueDecoder::ReadOpaque(flecs::entity_t type, ValueBytes value) {
  const auto opaque = Meta<EcsOpaque>(World(), type);
  if (!opaque) {
    return Error(opaque.error());
  }
  const auto as_type = Meta<EcsType>(World(), opaque->get().as_type);
  if (!as_type) {
    return Error(as_type.error());
  }

  Status read = Error("not a primitive or collection");
  switch (const ecs_type_kind_t kind = as_type->get().kind) {
    case EcsPrimitiveType:
      read = Meta<EcsPrimitive>(World(), opaque->get().as_type).and_then([&](const EcsPrimitive& primitive) {
        return AssignOpaquePrimitive(*opaque, primitive.kind, value);
      });
      break;
    case EcsArrayType:
      [[fallthrough]];
    case EcsVectorType:
      read = ReadOpaqueElements(*opaque, kind, value);
      break;
    default:
      break;
  }
  if (!read) {
    return Error(std::format("opaque '{}': {}", TypeName(World(), type), read.error()));
  }
  return {};
}

Status ValueDecoder::ReadOpaqueElements(const EcsOpaque& opaque, ecs_type_kind_t as_kind, ValueBytes value) {
  const bool is_array = as_kind == EcsArrayType;
  std::optional<size_t> array_count;
  flecs::entity_t element_type {};
  if (is_array) {
    const auto array = Meta<EcsArray>(World(), opaque.as_type);
    if (!array) {
      return Error(array.error());
    }
    element_type = array->get().type;
    array_count = static_cast<size_t>(array->get().count);
  } else {
    const auto vector = Meta<EcsVector>(World(), opaque.as_type);
    if (!vector) {
      return Error(vector.error());
    }
    element_type = vector->get().type;
  }

  const auto count = ReadCount();
  if (!count) {
    return Error(count.error());
  }
  if (array_count && *count != *array_count) {
    return Error("array element count mismatch");
  }
  if (opaque.ensure_element == nullptr || (!is_array && opaque.resize == nullptr)) {
    return Error("opaque collection can't be assigned");
  }
  if (opaque.resize != nullptr) {
    opaque.resize(value.data(), *count);
  }
  for (size_t i = 0; i < *count; ++i) {
    const auto read = ViewValue(World(), element_type, opaque.ensure_element(value.data(), i))
                          .and_then([&](ValueBytes element) { return Read(element_type, element); });
    if (!read) {
      return read;
    }
  }
  return {};
}

Status ValueDecoder::AssignOpaquePrimitive(const EcsOpaque& opaque, ecs_primitive_kind_t kind, ValueBytes value) {
  const auto assign = [&]<class T>(auto callback, std::expected<T, std::string> scalar, auto... args) -> Status {
    if (callback == nullptr) {
      return Error("opaque type can't be assigned this primitive");
    }
    if (!scalar) {
      return Error(scalar.error());
    }
    callback(value.data(), args..., *scalar);
    return {};
  };
  switch (kind) {
    case EcsBool:
      return assign(opaque.assign_bool, ReadScalar<uint8_t>().transform([](uint8_t byte) { return byte != 0; }));
    case EcsChar:
      return assign(opaque.assign_char, ReadScalar<char>());
    case EcsByte:
      [[fallthrough]];
    case EcsU8:
      return assign(opaque.assign_uint, ReadScalar<uint8_t>());
    case EcsU16:
      return assign(opaque.assign_uint, ReadScalar<uint16_t>());
    case EcsU32:
      return assign(opaque.assign_uint, ReadScalar<uint32_t>());
    case EcsU64:
      [[fallthrough]];
    case EcsUPtr:
      return assign(opaque.assign_uint, ReadScalar<uint64_t>());
    case EcsI8:
      return assign(opaque.assign_int, ReadScalar<int8_t>());
    case EcsI16:
      return assign(opaque.assign_int, ReadScalar<int16_t>());
    case EcsI32:
      return assign(opaque.assign_int, ReadScalar<int32_t>());
    case EcsI64:
      [[fallthrough]];
    case EcsIPtr:
      return assign(opaque.assign_int, ReadScalar<int64_t>());
    case EcsF32:
      return assign(opaque.assign_float, ReadScalar<float>());
    case EcsF64:
      return assign(opaque.assign_float, ReadScalar<double>());
    case EcsString: {
      if (opaque.assign_string == nullptr) {
        return Error("opaque type can't be assigned a string");
      }
      const auto str = ReadString();
      if (!str) {
        return Error(str.error());
      }
      opaque.assign_string(value.data(), str->c_str());
      return {};
    }
    case EcsEntity:
      return assign(opaque.assign_entity, ReadEntity(), World().c_ptr());
    default:
      return Error("opaque primitive kind is not supported");
  }
}

}  // namespace z13::flecs_tools::codec
