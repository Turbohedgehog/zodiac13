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

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <flecs.h>

namespace z13::flecs_tools {

// Binary component values, written by walking the type's flecs meta (little-endian
// primitives, length-prefixed strings and collections, entities by path). Supports
// primitives, enums, bitmasks, structs, arrays and opaque types serialized as a
// primitive, array or vector; anything else is an error rather than silently dropped.
std::expected<std::vector<uint8_t>, std::string> EncodeValue(
    const flecs::world& world, flecs::entity_t type, const void* value);

// Decodes into an existing instance of `type`. Bytes may be untrusted: truncated,
// trailing or out-of-range data is an error, though `value` may then be partly written.
std::expected<void, std::string> DecodeValue(
    const flecs::world& world, flecs::entity_t type, void* value, std::span<const uint8_t> bytes);

// Checks the bytes decode as `type`, without touching any live component.
std::expected<void, std::string> ValidateValue(
    const flecs::world& world, flecs::entity_t type, std::span<const uint8_t> bytes);

// Conversions to and from flecs meta JSON, for saves and debug dumps.
std::expected<std::string, std::string> ValueToJson(
    const flecs::world& world, flecs::entity_t type, std::span<const uint8_t> bytes);
std::expected<std::vector<uint8_t>, std::string> ValueFromJson(
    const flecs::world& world, flecs::entity_t type, std::string_view json);

}  // namespace z13::flecs_tools
