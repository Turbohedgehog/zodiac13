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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace z13::net {

constexpr size_t kByteBits = 8;

// Little-endian, a byte at a time.
template <typename T>
void Put(std::vector<uint8_t>& out, T value) {
  for (size_t i = 0; i < sizeof(T); ++i) {
    out.push_back(static_cast<uint8_t>(value >> (i * kByteBits)));
  }
}

template <typename T>
T Get(std::span<const uint8_t> bytes, size_t at) {
  T value = 0;
  for (size_t i = 0; i < sizeof(T); ++i) {
    value |= static_cast<T>(static_cast<T>(bytes[at + i]) << (i * kByteBits));
  }
  return value;
}

// The tick with `low` as its low 32 bits that is nearest `now`.
inline uint64_t ResolveTick(uint64_t now, uint32_t low) {
  const int64_t ahead = static_cast<int32_t>(low - static_cast<uint32_t>(now));
  return static_cast<uint64_t>(std::max<int64_t>(static_cast<int64_t>(now) + ahead, 0));
}

}  // namespace z13::net
