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

#include "scratch_value.h"

#include <utility>

namespace z13::flecs_tools::codec {

std::expected<ScratchValue, std::string> ScratchValue::Create(const flecs::world& world, flecs::entity_t type) {
  return ViewValue(world, type, ecs_value_new(world.c_ptr(), type)).transform([&](ValueBytes bytes) {
    return ScratchValue(world, type, bytes);
  });
}

ScratchValue::ScratchValue(const flecs::world& world, flecs::entity_t type, ValueBytes bytes)
    : world_(world), type_(type), bytes_(bytes) {}

ScratchValue::ScratchValue(ScratchValue&& other) noexcept
    : world_(other.world_), type_(other.type_), bytes_(std::exchange(other.bytes_, {})) {}

ScratchValue::~ScratchValue() {
  if (bytes_.data() != nullptr) {
    ecs_value_free(world_.c_ptr(), type_, bytes_.data());
  }
}

}  // namespace z13::flecs_tools::codec
