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

#include <z13_rooms/topology_fingerprint.h>

#include <array>

namespace z13::station::rooms {

namespace {

// The SplitMix64 finalizer.
uint64_t Mix(uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}

}  // namespace

uint64_t BlockFingerprint(const Block& block) {
  const std::array<uint64_t, 8> fields {
      block.spec.type_id,
      static_cast<uint64_t>(block.spec.orientation),
      static_cast<uint64_t>(block.spec.size.x()),
      static_cast<uint64_t>(block.spec.size.y()),
      static_cast<uint64_t>(block.spec.size.z()),
      static_cast<uint64_t>(block.cell.x()),
      static_cast<uint64_t>(block.cell.y()),
      static_cast<uint64_t>(block.cell.z()),
  };
  uint64_t hash {};
  for (const uint64_t field : fields) {
    hash = Mix(hash ^ field);
  }
  return hash;
}

}  // namespace z13::station::rooms
