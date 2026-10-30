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


#include "conduit_kinds.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <unordered_set>

#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>

#include <conduit_generated.h>

#include <primitives/palette.h>
#include <primitives/station_assets.h>

namespace z13::conduit {

namespace {

namespace fbs = z13::fbs::conduit;

std::unexpected<std::string> Error(std::string message) {
  return std::unexpected(std::format("conduits: {}", message));
}

}  // namespace

std::filesystem::path ConduitKindsFile() {
  return std::filesystem::path(z13::building::primitives::kStationDir) / kConduitKindsFileName;
}

std::expected<ConduitKinds, std::string> ParseConduitKinds(std::string_view json) {
  flatbuffers::Parser parser;
  if (!parser.Deserialize(reflection::GetSchema(fbs::ConduitKindsBinarySchema::data()))) {
    return Error("failed to deserialize the binary schema");
  }
  if (!parser.Parse(std::string(json).c_str())) {
    return Error(parser.error_);
  }
  fbs::ConduitKindsT source;
  fbs::GetConduitKinds(parser.builder_.GetBufferPointer())->UnPackTo(&source);
  if (source.kinds.empty()) {
    return Error("no kinds");
  }
  ConduitKinds kinds;
  std::unordered_set<uint32_t> ids;
  std::unordered_set<std::string> names;
  for (const auto& entry : source.kinds) {
    if (entry->network.empty()) {
      return Error(std::format("kind {} has no network", entry->id));
    }
    if (!entry->color) {
      return Error(std::format("kind {} has no colour", entry->id));
    }
    if (!ids.insert(entry->id).second || !names.insert(entry->name).second) {
      return Error(std::format("kind {} '{}' repeats an id or a name", entry->id, entry->name));
    }
    kinds.kinds.push_back({.id = entry->id,
                           .name = entry->name,
                           .network = entry->network,
                           .thickness_cells = entry->thickness_cells,
                           .color = z13::building::primitives::ToRgba(*entry->color)});
  }
  return kinds;
}

}  // namespace z13::conduit
