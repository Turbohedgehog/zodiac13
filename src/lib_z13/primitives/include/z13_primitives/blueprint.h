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

#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <z13/components/station.h>
#include <z13_primitives/palette.h>

namespace z13::building::primitives {

// Parses blueprint JSON (blueprint.fbs) into blocks of `palette`, naming the first block
// whose primitive the palette lacks. Placement isn't checked here: that's ValidateBuild's.
std::expected<std::vector<z13::station::Block>, std::string> ParseBlueprint(
    std::string_view json, const Palette& palette);

}  // namespace z13::building::primitives
