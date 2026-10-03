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

#include <lib_core/component_codec.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <utility>

namespace z13::flecs_tools {

namespace {

// Every supported platform (x64, arm64) is little-endian, so primitives are copied as-is.
static_assert(std::endian::native == std::endian::little);

using Error = std::unexpected<std::string>;
using Status = std::expected<void, std::string>;

// Pointer-sized integers are always written as 8 bytes, so the format doesn't depend on the platform.
using WideUPtr = uint64_t;
using WideIPtr = int64_t;

template <class T>
const T* Meta(const flecs::world& world, flecs::entity_t type, flecs::entity_t meta_component) {
  return static_cast<const T*>(ecs_get_id(world.c_ptr(), type, meta_component));
}

std::string TypeName(const flecs::world& world, flecs::entity_t type) {
  return flecs::entity(world.c_ptr(), type).path().c_str();
}

std::optional<size_t> TypeSize(const flecs::world& world, flecs::entity_t type) {
  const ecs_type_info_t* info = ecs_get_type_info(world.c_ptr(), type);
  if (info == nullptr) {
    return std::nullopt;
  }
  return static_cast<size_t>(info->size);
}

std::optional<size_t> FixedSize(ecs_primitive_kind_t kind) {
  switch (kind) {
    case EcsBool:
    case EcsChar:
    case EcsByte:
    case EcsU8:
    case EcsI8:
      return 1;
    case EcsU16:
    case EcsI16:
      return 2;
    case EcsU32:
    case EcsI32:
    case EcsF32:
      return 4;
    case EcsU64:
    case EcsI64:
    case EcsF64:
      return 8;
    default:
      return std::nullopt;
  }
}

// An instance of a meta type outside any entity, for decoding or JSON conversion.
class ScratchValue {
 public:
  ScratchValue(const flecs::world& world, flecs::entity_t type)
      : world_(world.c_ptr()), type_(type), value_(ecs_value_new(world_, type)) {}
  ~ScratchValue() {
    if (value_ != nullptr) {
      ecs_value_free(world_, type_, value_);
    }
  }
  ScratchValue(const ScratchValue&) = delete;
  ScratchValue& operator=(const ScratchValue&) = delete;

  void* Get() const { return value_; }

 private:
  ecs_world_t* world_;
  flecs::entity_t type_;
  void* value_;
};

class Encoder {
 public:
  explicit Encoder(const flecs::world& world) : world_(world) {}

  Status Write(flecs::entity_t type, const void* value) {
    const EcsType* meta = Meta<EcsType>(world_, type, ecs_id(EcsType));
    if (meta == nullptr) {
      return Error(std::format("'{}' has no meta", TypeName(world_, type)));
    }
    switch (meta->kind) {
      case EcsPrimitiveType:
        return WritePrimitive(type, Meta<EcsPrimitive>(world_, type, ecs_id(EcsPrimitive))->kind, value);
      case EcsEnumType:
        return Write(Meta<EcsEnum>(world_, type, ecs_id(EcsEnum))->underlying_type, value);
      case EcsBitmaskType:
        Append(value, sizeof(uint32_t));
        return {};
      case EcsStructType:
        return WriteStruct(*Meta<EcsStruct>(world_, type, ecs_id(EcsStruct)), value);
      case EcsArrayType: {
        const EcsArray& array = *Meta<EcsArray>(world_, type, ecs_id(EcsArray));
        return WriteElements(array.type, array.count, value);
      }
      case EcsOpaqueType:
        return WriteOpaque(type, value);
      default:
        return Error(std::format("'{}' has an unsupported meta kind", TypeName(world_, type)));
    }
  }

  std::vector<uint8_t> Take() { return std::move(bytes_); }

 private:
  // Collects what an opaque type's serialize callback emits.
  struct OpaqueSink {
    Encoder& encoder;
    size_t count {};
    Status status;
  };

  static int OnOpaqueValue(const ecs_serializer_t* ser, ecs_entity_t type, const void* value) {
    auto& sink = *static_cast<OpaqueSink*>(ser->ctx);
    ++sink.count;
    sink.status = sink.encoder.Write(type, value);
    return sink.status ? 0 : -1;
  }

  static int OnOpaqueMember(const ecs_serializer_t* ser, const char*) {
    static_cast<OpaqueSink*>(ser->ctx)->status = Error("opaque structs are not supported");
    return -1;
  }

  Status WritePrimitive(flecs::entity_t type, ecs_primitive_kind_t kind, const void* value) {
    switch (kind) {
      case EcsString: {
        const char* str = *static_cast<const char* const*>(value);
        AppendString(str == nullptr ? std::string_view{} : std::string_view{str});
        return {};
      }
      case EcsEntity: {
        const flecs::entity_t entity = *static_cast<const flecs::entity_t*>(value);
        AppendString(entity == 0 ? std::string{} : TypeName(world_, entity));
        return {};
      }
      case EcsUPtr: {
        const WideUPtr wide = *static_cast<const uintptr_t*>(value);
        Append(&wide, sizeof(wide));
        return {};
      }
      case EcsIPtr: {
        const WideIPtr wide = *static_cast<const intptr_t*>(value);
        Append(&wide, sizeof(wide));
        return {};
      }
      default:
        break;
    }
    const std::optional<size_t> size = FixedSize(kind);
    if (!size) {
      return Error(std::format("primitive '{}' is not supported", TypeName(world_, type)));
    }
    Append(value, *size);
    return {};
  }

  Status WriteStruct(const EcsStruct& meta, const void* value) {
    const auto* members = static_cast<const ecs_member_t*>(ecs_vec_first(&meta.members));
    for (const ecs_member_t& member : std::span(members, ecs_vec_count(&meta.members))) {
      const auto* field = static_cast<const std::byte*>(value) + member.offset;
      if (auto written = WriteElements(member.type, std::max(member.count, 1), field); !written) {
        return written;
      }
    }
    return {};
  }

  Status WriteElements(flecs::entity_t type, int32_t count, const void* first) {
    const std::optional<size_t> size = TypeSize(world_, type);
    if (!size) {
      return Error(std::format("'{}' has no type info", TypeName(world_, type)));
    }
    for (int32_t i = 0; i < count; ++i) {
      if (auto written = Write(type, static_cast<const std::byte*>(first) + i * *size); !written) {
        return written;
      }
    }
    return {};
  }

  // Opaque values are written as their `as_type`: a primitive as is, a collection
  // with its element count first.
  Status WriteOpaque(flecs::entity_t type, const void* value) {
    const EcsOpaque& opaque = *Meta<EcsOpaque>(world_, type, ecs_id(EcsOpaque));
    const EcsType* as_type = Meta<EcsType>(world_, opaque.as_type, ecs_id(EcsType));
    if (as_type == nullptr || opaque.serialize == nullptr) {
      return Error(std::format("opaque '{}' has no serializer", TypeName(world_, type)));
    }

    Encoder elements(world_);
    OpaqueSink sink {.encoder = elements};
    ecs_serializer_t serializer {};
    serializer.value_ = &OnOpaqueValue;
    serializer.member_ = &OnOpaqueMember;
    serializer.world = world_.c_ptr();
    serializer.ctx = &sink;
    if (opaque.serialize(&serializer, value) != 0 || !sink.status) {
      return Error(std::format("opaque '{}': {}", TypeName(world_, type),
                               sink.status ? "serialize failed" : sink.status.error()));
    }

    if (as_type->kind == EcsArrayType || as_type->kind == EcsVectorType) {
      AppendCount(sink.count);
    } else if (as_type->kind != EcsPrimitiveType || sink.count != 1) {
      return Error(std::format("opaque '{}' is not a primitive or collection", TypeName(world_, type)));
    }
    const std::vector<uint8_t> bytes = elements.Take();
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    return {};
  }

  void Append(const void* data, size_t size) {
    const auto* begin = static_cast<const uint8_t*>(data);
    bytes_.insert(bytes_.end(), begin, begin + size);
  }

  void AppendCount(size_t count) {
    const auto narrow = static_cast<uint32_t>(count);
    Append(&narrow, sizeof(narrow));
  }

  void AppendString(std::string_view str) {
    AppendCount(str.size());
    Append(str.data(), str.size());
  }

  const flecs::world& world_;
  std::vector<uint8_t> bytes_;
};

class Decoder {
 public:
  Decoder(const flecs::world& world, std::span<const uint8_t> bytes) : world_(world), bytes_(bytes) {}

  Status Read(flecs::entity_t type, void* value) {
    const EcsType* meta = Meta<EcsType>(world_, type, ecs_id(EcsType));
    if (meta == nullptr) {
      return Error(std::format("'{}' has no meta", TypeName(world_, type)));
    }
    switch (meta->kind) {
      case EcsPrimitiveType:
        return ReadPrimitive(type, Meta<EcsPrimitive>(world_, type, ecs_id(EcsPrimitive))->kind, value);
      case EcsEnumType:
        return Read(Meta<EcsEnum>(world_, type, ecs_id(EcsEnum))->underlying_type, value);
      case EcsBitmaskType:
        return ReadInto(value, sizeof(uint32_t));
      case EcsStructType:
        return ReadStruct(*Meta<EcsStruct>(world_, type, ecs_id(EcsStruct)), value);
      case EcsArrayType: {
        const EcsArray& array = *Meta<EcsArray>(world_, type, ecs_id(EcsArray));
        return ReadElements(array.type, array.count, value);
      }
      case EcsOpaqueType:
        return ReadOpaque(type, value);
      default:
        return Error(std::format("'{}' has an unsupported meta kind", TypeName(world_, type)));
    }
  }

  bool AtEnd() const { return bytes_.empty(); }

 private:
  Status ReadInto(void* dst, size_t size) {
    if (bytes_.size() < size) {
      return Error("truncated value");
    }
    std::memcpy(dst, bytes_.data(), size);
    bytes_ = bytes_.subspan(size);
    return {};
  }

  template <class T>
  std::expected<T, std::string> ReadScalar() {
    T scalar {};
    if (auto read = ReadInto(&scalar, sizeof(T)); !read) {
      return Error(read.error());
    }
    return scalar;
  }

  // Every element takes at least one byte, so a count beyond the remaining bytes is corrupt.
  std::expected<size_t, std::string> ReadCount() {
    const auto count = ReadScalar<uint32_t>();
    if (count && *count > bytes_.size()) {
      return Error("count exceeds the remaining bytes");
    }
    return count;
  }

  std::expected<std::string, std::string> ReadString() {
    const auto size = ReadCount();
    if (!size) {
      return Error(size.error());
    }
    std::string str(reinterpret_cast<const char*>(bytes_.data()), *size);
    bytes_ = bytes_.subspan(*size);
    return str;
  }

  std::expected<flecs::entity_t, std::string> ReadEntity() {
    const auto path = ReadString();
    if (!path || path->empty()) {
      return path ? std::expected<flecs::entity_t, std::string>(0) : Error(path.error());
    }
    const flecs::entity entity = world_.lookup(path->c_str());
    if (!entity) {
      return Error(std::format("unknown entity '{}'", *path));
    }
    return entity.id();
  }

  Status ReadPrimitive(flecs::entity_t type, ecs_primitive_kind_t kind, void* value) {
    switch (kind) {
      case EcsBool: {
        const auto byte = ReadScalar<uint8_t>();
        if (byte) {
          *static_cast<bool*>(value) = *byte != 0;
        }
        return byte ? Status{} : Error(byte.error());
      }
      case EcsString: {
        const auto str = ReadString();
        if (!str) {
          return Error(str.error());
        }
        auto& dst = *static_cast<char**>(value);
        ecs_os_free(dst);
        dst = ecs_os_strdup(str->c_str());
        return {};
      }
      case EcsEntity: {
        const auto entity = ReadEntity();
        if (entity) {
          *static_cast<flecs::entity_t*>(value) = *entity;
        }
        return entity ? Status{} : Error(entity.error());
      }
      case EcsUPtr: {
        const auto wide = ReadScalar<WideUPtr>();
        if (wide) {
          *static_cast<uintptr_t*>(value) = static_cast<uintptr_t>(*wide);
        }
        return wide ? Status{} : Error(wide.error());
      }
      case EcsIPtr: {
        const auto wide = ReadScalar<WideIPtr>();
        if (wide) {
          *static_cast<intptr_t*>(value) = static_cast<intptr_t>(*wide);
        }
        return wide ? Status{} : Error(wide.error());
      }
      default:
        break;
    }
    const std::optional<size_t> size = FixedSize(kind);
    if (!size) {
      return Error(std::format("primitive '{}' is not supported", TypeName(world_, type)));
    }
    return ReadInto(value, *size);
  }

  Status ReadStruct(const EcsStruct& meta, void* value) {
    const auto* members = static_cast<const ecs_member_t*>(ecs_vec_first(&meta.members));
    for (const ecs_member_t& member : std::span(members, ecs_vec_count(&meta.members))) {
      auto* field = static_cast<std::byte*>(value) + member.offset;
      if (auto read = ReadElements(member.type, std::max(member.count, 1), field); !read) {
        return read;
      }
    }
    return {};
  }

  Status ReadElements(flecs::entity_t type, int32_t count, void* first) {
    const std::optional<size_t> size = TypeSize(world_, type);
    if (!size) {
      return Error(std::format("'{}' has no type info", TypeName(world_, type)));
    }
    for (int32_t i = 0; i < count; ++i) {
      if (auto read = Read(type, static_cast<std::byte*>(first) + i * *size); !read) {
        return read;
      }
    }
    return {};
  }

  // Reads a primitive of `kind` and hands it to the matching assign_* callback.
  Status AssignOpaquePrimitive(const EcsOpaque& opaque, ecs_primitive_kind_t kind, void* value) {
    const auto assign = [&](auto callback, auto read, auto&&... args) -> Status {
      if (callback == nullptr) {
        return Error("opaque type can't be assigned this primitive");
      }
      const auto scalar = read();
      if (!scalar) {
        return Error(scalar.error());
      }
      callback(value, args..., *scalar);
      return {};
    };
    switch (kind) {
      case EcsBool:
        return assign(opaque.assign_bool, [this] {
          return ReadScalar<uint8_t>().transform([](uint8_t byte) { return byte != 0; });
        });
      case EcsChar:
        return assign(opaque.assign_char, [this] { return ReadScalar<char>(); });
      case EcsByte:
      case EcsU8:
        return assign(opaque.assign_uint, [this] { return ReadScalar<uint8_t>(); });
      case EcsU16:
        return assign(opaque.assign_uint, [this] { return ReadScalar<uint16_t>(); });
      case EcsU32:
        return assign(opaque.assign_uint, [this] { return ReadScalar<uint32_t>(); });
      case EcsU64:
      case EcsUPtr:
        return assign(opaque.assign_uint, [this] { return ReadScalar<uint64_t>(); });
      case EcsI8:
        return assign(opaque.assign_int, [this] { return ReadScalar<int8_t>(); });
      case EcsI16:
        return assign(opaque.assign_int, [this] { return ReadScalar<int16_t>(); });
      case EcsI32:
        return assign(opaque.assign_int, [this] { return ReadScalar<int32_t>(); });
      case EcsI64:
      case EcsIPtr:
        return assign(opaque.assign_int, [this] { return ReadScalar<int64_t>(); });
      case EcsF32:
        return assign(opaque.assign_float, [this] { return ReadScalar<float>(); });
      case EcsF64:
        return assign(opaque.assign_float, [this] { return ReadScalar<double>(); });
      case EcsString: {
        if (opaque.assign_string == nullptr) {
          return Error("opaque type can't be assigned a string");
        }
        const auto str = ReadString();
        if (!str) {
          return Error(str.error());
        }
        opaque.assign_string(value, str->c_str());
        return {};
      }
      case EcsEntity:
        return assign(opaque.assign_entity, [this] { return ReadEntity(); }, world_.c_ptr());
      default:
        return Error("opaque primitive kind is not supported");
    }
  }

  Status ReadOpaqueElements(const EcsOpaque& opaque, const EcsType& as_type, void* value) {
    const bool is_array = as_type.kind == EcsArrayType;
    const flecs::entity_t element_type = is_array
        ? Meta<EcsArray>(world_, opaque.as_type, ecs_id(EcsArray))->type
        : Meta<EcsVector>(world_, opaque.as_type, ecs_id(EcsVector))->type;
    const auto count = ReadCount();
    if (!count) {
      return Error(count.error());
    }
    if (is_array && std::cmp_not_equal(*count, Meta<EcsArray>(world_, opaque.as_type, ecs_id(EcsArray))->count)) {
      return Error("array element count mismatch");
    }
    if (opaque.ensure_element == nullptr || (!is_array && opaque.resize == nullptr)) {
      return Error("opaque collection can't be assigned");
    }
    if (opaque.resize != nullptr) {
      opaque.resize(value, *count);
    }
    for (size_t i = 0; i < *count; ++i) {
      void* element = opaque.ensure_element(value, i);
      if (element == nullptr) {
        return Error("opaque collection rejected an element");
      }
      if (auto read = Read(element_type, element); !read) {
        return read;
      }
    }
    return {};
  }

  Status ReadOpaque(flecs::entity_t type, void* value) {
    const EcsOpaque& opaque = *Meta<EcsOpaque>(world_, type, ecs_id(EcsOpaque));
    const EcsType* as_type = Meta<EcsType>(world_, opaque.as_type, ecs_id(EcsType));
    if (as_type == nullptr) {
      return Error(std::format("opaque '{}' has no as_type", TypeName(world_, type)));
    }

    Status read = Error(std::format("opaque '{}' is not a primitive or collection", TypeName(world_, type)));
    if (as_type->kind == EcsPrimitiveType) {
      read = AssignOpaquePrimitive(
          opaque, Meta<EcsPrimitive>(world_, opaque.as_type, ecs_id(EcsPrimitive))->kind, value);
    } else if (as_type->kind == EcsArrayType || as_type->kind == EcsVectorType) {
      read = ReadOpaqueElements(opaque, *as_type, value);
    }
    if (!read) {
      return Error(std::format("opaque '{}': {}", TypeName(world_, type), read.error()));
    }
    return {};
  }

  const flecs::world& world_;
  std::span<const uint8_t> bytes_;
};

}  // namespace

std::expected<std::vector<uint8_t>, std::string> EncodeValue(
    const flecs::world& world, flecs::entity_t type, const void* value) {
  Encoder encoder(world);
  if (auto written = encoder.Write(type, value); !written) {
    return Error(written.error());
  }
  return encoder.Take();
}

std::expected<void, std::string> DecodeValue(
    const flecs::world& world, flecs::entity_t type, void* value, std::span<const uint8_t> bytes) {
  Decoder decoder(world, bytes);
  if (auto read = decoder.Read(type, value); !read) {
    return read;
  }
  if (!decoder.AtEnd()) {
    return Error("trailing bytes after value");
  }
  return {};
}

std::expected<void, std::string> ValidateValue(
    const flecs::world& world, flecs::entity_t type, std::span<const uint8_t> bytes) {
  const ScratchValue scratch(world, type);
  if (scratch.Get() == nullptr) {
    return Error(std::format("'{}' can't be instantiated", TypeName(world, type)));
  }
  return DecodeValue(world, type, scratch.Get(), bytes);
}

std::expected<std::string, std::string> ValueToJson(
    const flecs::world& world, flecs::entity_t type, std::span<const uint8_t> bytes) {
  const ScratchValue scratch(world, type);
  if (scratch.Get() == nullptr) {
    return Error(std::format("'{}' can't be instantiated", TypeName(world, type)));
  }
  if (auto read = DecodeValue(world, type, scratch.Get(), bytes); !read) {
    return Error(read.error());
  }
  const flecs::string json = world.to_json(type, scratch.Get());
  if (json.size() == 0) {
    return Error(std::format("'{}' has no JSON serializer", TypeName(world, type)));
  }
  return std::string(json.c_str());
}

std::expected<std::vector<uint8_t>, std::string> ValueFromJson(
    const flecs::world& world, flecs::entity_t type, std::string_view json) {
  const ScratchValue scratch(world, type);
  if (scratch.Get() == nullptr) {
    return Error(std::format("'{}' can't be instantiated", TypeName(world, type)));
  }
  const std::string owned(json);
  flecs::world mutable_world = world;  // from_json isn't const
  if (mutable_world.from_json(type, scratch.Get(), owned.c_str()) == nullptr) {
    return Error(std::format("invalid JSON value for '{}'", TypeName(world, type)));
  }
  return EncodeValue(world, type, scratch.Get());
}

}  // namespace z13::flecs_tools
