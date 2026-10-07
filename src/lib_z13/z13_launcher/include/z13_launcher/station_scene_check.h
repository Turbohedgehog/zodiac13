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

#include <filesystem>

#include <lib_core/settings/config.h>
#include <lib_core/utils/status.h>

namespace z13 {

// The blueprint --station-scene names must lie under `assets_dir`; checked before any module
// loads, so a mistyped name stops the launch instead of leaving an empty station.
Status CheckStationScene(const Config& config, const std::filesystem::path& assets_dir);

}  // namespace z13
