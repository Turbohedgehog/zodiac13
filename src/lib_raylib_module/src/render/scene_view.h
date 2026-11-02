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

#include <raylib.h>

#include <optional>

#include <rooms/room_cache.h>
#include <rooms/view_culling.h>

namespace z13::raylib {

// What a frame sees from: the eye and the rooms the camera's view reaches.
struct SceneView {
  ::Vector3 eye {};
  z13::station::rooms::ViewCulling culling;
};

inline z13::station::rooms::ViewCulling::OptionalGraph CurrentRooms(const z13::station::rooms::RoomCache* cache) {
  return cache != nullptr ? cache->Current() : std::nullopt;
}

}  // namespace z13::raylib
