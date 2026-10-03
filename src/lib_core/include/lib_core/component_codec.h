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
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>

namespace z13::flecs_tools {

// Binary component values, written by walking the type's flecs meta (docs/serialization-plan.md).
// `value` is the component's memory and must be exactly its type's size.
std::expected<std::vector<uint8_t>, std::string> EncodeValue(
    const flecs::world& world, flecs::entity_t type, std::span<const std::byte> value);

// Bytes may be untrusted; on error `value` may be partly written.
std::expected<void, std::string> DecodeValue(
    const flecs::world& world, flecs::entity_t type, std::span<std::byte> value, std::span<const uint8_t> bytes);

// Views component memory flecs hands out (try_get, ensure) as a value of `type`; an error if null.
std::expected<std::span<std::byte>, std::string> ComponentBytes(
    const flecs::world& world, flecs::entity_t type, void* value);
std::expected<std::span<const std::byte>, std::string> ComponentBytes(
    const flecs::world& world, flecs::entity_t type, const void* value);

// Encodes a default value of `type`, to catch member types the codec can't write.
std::expected<void, std::string> CheckEncodable(const flecs::world& world, flecs::entity_t type);

std::expected<void, std::string> ValidateValue(
    const flecs::world& world, flecs::entity_t type, std::span<const uint8_t> bytes);

std::expected<std::string, std::string> ValueToJson(
    const flecs::world& world, flecs::entity_t type, std::span<const uint8_t> bytes);
std::expected<std::vector<uint8_t>, std::string> ValueFromJson(
    const flecs::world& world, flecs::entity_t type, std::string_view json);

}  // namespace z13::flecs_tools
