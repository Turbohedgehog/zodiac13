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

#include <functional>
#include <string>
#include <vector>

#include <flecs.h>

namespace z13 {

constexpr double kMsPerSecond = 1000.0;

// Time one system has spent, in milliseconds.
struct SystemTime {
  std::string path;
  double ms {};
};

using SystemSampler = std::function<std::vector<SystemTime>()>;

// Every system's time so far; flecs counts it only while ecs_measure_system_time is on.
std::vector<SystemTime> SystemTimes(flecs::world world);

// What each system spent between two SystemTimes, per frame, heaviest first.
std::vector<SystemTime> SystemTimesPerFrame(
    const std::vector<SystemTime>& before, const std::vector<SystemTime>& after, int frames);

}  // namespace z13