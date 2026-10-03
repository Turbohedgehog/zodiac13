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
#include <functional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <flecs.h>

#include "meta_access.h"

namespace z13::flecs_tools::codec {

class ValueEncoder {
 public:
  explicit ValueEncoder(const flecs::world& world) : world_(world) {}

  Status Write(flecs::entity_t type, ConstValueBytes value);
  std::vector<uint8_t> Take() { return std::move(bytes_); }

 private:
  struct OpaqueSink {
    std::reference_wrapper<ValueEncoder> encoder;
    size_t count {};
    Status status {};
  };

  // flecs' ecs_serializer_t callbacks.
  static int OnOpaqueValue(const ecs_serializer_t* serializer, ecs_entity_t type, const void* value);
  static int OnOpaqueMember(const ecs_serializer_t* serializer, const char* name);

  const flecs::world& World() const { return world_.get(); }

  Status WritePrimitive(flecs::entity_t type, ConstValueBytes value);
  Status WriteStruct(flecs::entity_t type, ConstValueBytes value);
  Status WriteElements(flecs::entity_t type, size_t count, ConstValueBytes values);
  Status WriteOpaque(flecs::entity_t type, ConstValueBytes value);

  void Append(std::span<const std::byte> data);
  template <class T>
  void AppendScalar(const T& scalar) {
    Append(std::as_bytes(std::span(&scalar, 1)));
  }
  void AppendCount(size_t count) { AppendScalar(static_cast<uint32_t>(count)); }
  void AppendString(std::string_view str);

  std::reference_wrapper<const flecs::world> world_;
  std::vector<uint8_t> bytes_ {};
};

}  // namespace z13::flecs_tools::codec
