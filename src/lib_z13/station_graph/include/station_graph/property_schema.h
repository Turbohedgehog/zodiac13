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
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <lib_core/utils/status.h>

#include "station_graph/graph.h"

namespace z13::editor::graph {

enum class PropertyKind : std::uint8_t { kInt, kBool, kEnum };

// One property of a node or an edge, as assets/station/graph/properties.json describes it. The editor's window,
// the checks and the list the assistant is given are built from these, so no property is named in code.
struct PropertySpec {
  PropertyKind kind {};
  std::string title;
  std::string unit;
  int min {};
  int max {};
  std::vector<std::string> values;
  PropertyValue default_value;
};

using PropertySpecs = std::map<std::string, PropertySpec>;

struct PropertySchema {
  PropertySpecs node;
  PropertySpecs edge;
};

std::expected<PropertySchema, std::string> ParsePropertySchema(std::string_view json);

// The value a command's text spells for the spec's kind ("true", "14", an enum's name), checked against the spec.
std::expected<PropertyValue, std::string> ParsePropertyValue(const PropertySpec& spec, std::string_view text);

z13::Status CheckProperty(const PropertySpec& spec, const PropertyValue& value);

z13::Status CheckProperties(const PropertySpecs& specs, const PropertyMap& given);

PropertyMap WithDefaults(const PropertySpecs& specs, const PropertyMap& given);

}  // namespace z13::editor::graph
