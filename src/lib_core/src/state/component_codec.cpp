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

#include <format>

#include "component_codec/meta_access.h"
#include "component_codec/scratch_value.h"
#include "component_codec/value_decoder.h"
#include "component_codec/value_encoder.h"

namespace z13::flecs_tools {

namespace {

using codec::Error;

std::expected<void, std::string> CheckSize(const flecs::world& world, flecs::entity_t type, size_t size) {
  return codec::TypeSize(world, type).and_then([&](size_t expected_size) -> std::expected<void, std::string> {
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

std::expected<void, std::string> DecodeValue(
    const flecs::world& world, flecs::entity_t type, std::span<std::byte> value, std::span<const uint8_t> bytes) {
  codec::ValueDecoder decoder(world, bytes);
  return CheckSize(world, type, value.size())
      .and_then([&] { return decoder.Read(type, value); })
      .and_then([&]() -> std::expected<void, std::string> {
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

std::expected<void, std::string> CheckEncodable(const flecs::world& world, flecs::entity_t type) {
  return codec::ScratchValue::Create(world, type).and_then([&](const codec::ScratchValue& scratch) {
    return EncodeValue(world, type, scratch.Bytes()).transform([](const std::vector<uint8_t>&) {});
  });
}

std::expected<void, std::string> ValidateValue(
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
