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
#include <functional>

namespace z13::station::rooms {

// How the room build spreads independent work (chunks, planes, leaves, portal sources):
// runs body(index) for every index in [0, count) and returns when all have. RoomSystem hands
// in its ThreadPool; results are gathered by index, so the graph doesn't depend on it.
using ParallelFor = std::function<void(size_t count, const std::function<void(size_t)>& body)>;

// In order, on the caller: the default, and what tests compare the pool against.
void Serial(size_t count, const std::function<void(size_t)>& body);

}  // namespace z13::station::rooms
