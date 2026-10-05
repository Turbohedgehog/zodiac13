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

namespace z13::station {

// The world is a station-building game rather than the ship scene. State, so a joining
// client gets the server's mode with Welcome's snapshot.
struct StationMode {
  using State = void;
  using Singleton = void;
};

// A spot SpawnPlayer puts players on; the entity's transform gives position and facing.
struct SpawnPoint {
  using State = void;
};

}  // namespace z13::station
