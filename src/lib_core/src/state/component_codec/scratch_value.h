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

#include <expected>
#include <string>

#include <flecs.h>

#include "meta_access.h"

namespace z13::flecs_tools::codec {

// A default-constructed value of a meta type, outside any entity.
class ScratchValue {
 public:
  static std::expected<ScratchValue, std::string> Create(const flecs::world& world, flecs::entity_t type);

  ScratchValue(ScratchValue&& other) noexcept;
  ScratchValue(const ScratchValue&) = delete;
  ScratchValue& operator=(const ScratchValue&) = delete;
  ScratchValue& operator=(ScratchValue&&) = delete;
  ~ScratchValue();

  ValueBytes Bytes() const { return bytes_; }

 private:
  ScratchValue(const flecs::world& world, flecs::entity_t type, ValueBytes bytes);

  flecs::world world_;
  flecs::entity_t type_ {};
  ValueBytes bytes_ {};
};

}  // namespace z13::flecs_tools::codec
