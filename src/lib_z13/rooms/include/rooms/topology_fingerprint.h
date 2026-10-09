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

#include <z13/components/station.h>

namespace z13::station::rooms {

// A hash of what makes one block differ from another on the grid. Summing these over the
// blocks that shape the rooms gives a fingerprint that doesn't depend on their order or on
// entity ids, which differ between peers.
uint64_t BlockFingerprint(const Block& block);

}  // namespace z13::station::rooms
