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
#include <span>

namespace z13 {

// FNV-1a: std::hash may differ between the participants' standard libraries.
inline constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
inline constexpr uint64_t kFnvPrime = 1099511628211ull;

constexpr uint64_t FnvMix(uint64_t hash, uint8_t byte) {
  return (hash ^ byte) * kFnvPrime;
}

inline uint64_t FnvHash(std::span<const std::byte> bytes, uint64_t hash = kFnvOffsetBasis) {
  for (const std::byte byte : bytes) {
    hash = FnvMix(hash, static_cast<uint8_t>(byte));
  }
  return hash;
}

}  // namespace z13
