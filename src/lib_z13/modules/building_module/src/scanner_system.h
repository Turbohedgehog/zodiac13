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

#include <flecs.h>

#include <z13/components/gameplay/inventory.h>

namespace z13::building {

// The scanner goes into the cell after the conduit tool's.
inline constexpr uint8_t kScannerCell = z13::inventory::kSpawnActiveCell + 2;

// A player was given the scanner; one who lost it doesn't get it back.
struct ScannerGiven {
  using State = void;
};

// The scanner: handed out with the starter kit in station mode; in the hand it reports the block the player aims at
// (its name, place, door state, power) as a `ScanReport` the HUD shows under the crosshair.
class ScannerSystem {
 public:
  static void Register(flecs::world& world);
};

}  // namespace z13::building
