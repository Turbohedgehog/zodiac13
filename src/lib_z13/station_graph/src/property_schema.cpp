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

#include "station_graph/property_schema.h"

#include <algorithm>
#include <format>
#include <optional>

#include <rfl.hpp>
#include <rfl/json.hpp>

namespace z13::editor::graph {

namespace {

constexpr std::string_view kIntType = "int";
constexpr std::string_view kBoolType = "bool";
constexpr std::string_view kEnumType = "enum";

// The file's shape; "default" is a keyword, so it is renamed.
struct PropertySpecJson {
  std::string type;
  std::string title;
  std::optional<std::string> unit;
  std::optional<int> min;
  std::optional<int> max;
  std::optional<std::vector<std::string>> values;
  rfl::Rename<"default", PropertyValue> default_value;
};

struct SchemaJson {
  int version {};
  std::map<std::string, PropertySpecJson> node;
  std::map<std::string, PropertySpecJson> edge;
};

std::expected<PropertySpec, std::string> SpecOf(const PropertySpecJson& json) {
  PropertySpec spec {.title = json.title,
                     .unit = json.unit.value_or(""),
                     .min = json.min.value_or(0),
                     .max = json.max.value_or(0),
                     .values = json.values.value_or(std::vector<std::string> {}),
                     .default_value = json.default_value.value()};
  if (json.type == kIntType) {
    spec.kind = PropertyKind::kInt;
  } else if (json.type == kBoolType) {
    spec.kind = PropertyKind::kBool;
  } else if (json.type == kEnumType) {
    spec.kind = PropertyKind::kEnum;
  } else {
    return std::unexpected(std::format("unknown type '{}'", json.type));
  }
  if (spec.kind == PropertyKind::kInt && (!json.min || !json.max || *json.min > *json.max)) {
    return std::unexpected("an int needs min and max, min not above max");
  }
  if (spec.kind == PropertyKind::kEnum && spec.values.empty()) {
    return std::unexpected("an enum needs values");
  }
  if (const z13::Status valid = CheckProperty(spec, spec.default_value); !valid) {
    return std::unexpected(std::format("the default: {}", valid.error()));
  }
  return spec;
}

std::expected<PropertySpecs, std::string> SpecsOf(const std::map<std::string, PropertySpecJson>& json,
                                                  std::string_view owner) {
  PropertySpecs specs;
  for (const auto& [name, spec_json] : json) {
    auto spec = SpecOf(spec_json);
    if (!spec) {
      return std::unexpected(std::format("{} property '{}': {}", owner, name, spec.error()));
    }
    specs.emplace(name, std::move(*spec));
  }
  return specs;
}

}  // namespace

std::expected<PropertySchema, std::string> ParsePropertySchema(std::string_view json) {
  const auto parsed = rfl::json::read<SchemaJson>(std::string(json));
  if (!parsed) {
    return std::unexpected(std::format("property schema: {}", parsed.error().what()));
  }
  if (parsed->version != kGraphVersion) {
    return std::unexpected(std::format("property schema version {}, expected {}", parsed->version, kGraphVersion));
  }
  auto node = SpecsOf(parsed->node, "node");
  if (!node) {
    return std::unexpected(node.error());
  }
  auto edge = SpecsOf(parsed->edge, "edge");
  if (!edge) {
    return std::unexpected(edge.error());
  }
  return PropertySchema {.node = std::move(*node), .edge = std::move(*edge)};
}

z13::Status CheckProperty(const PropertySpec& spec, const PropertyValue& value) {
  switch (spec.kind) {
    case PropertyKind::kBool:
      if (!std::holds_alternative<bool>(value)) {
        return std::unexpected("must be true or false");
      }
      return {};
    case PropertyKind::kInt: {
      const int* number = std::get_if<int>(&value);
      if (number == nullptr) {
        return std::unexpected("must be a whole number");
      }
      if (*number < spec.min || *number > spec.max) {
        return std::unexpected(std::format("is {}, allowed {}..{}", *number, spec.min, spec.max));
      }
      return {};
    }
    case PropertyKind::kEnum: {
      const std::string* text = std::get_if<std::string>(&value);
      if (text == nullptr || std::ranges::find(spec.values, *text) == spec.values.end()) {
        return std::unexpected(std::format("must be one of {}", rfl::json::write(spec.values)));
      }
      return {};
    }
  }
  return std::unexpected("unknown kind");
}

z13::Status CheckProperties(const PropertySpecs& specs, const PropertyMap& given) {
  for (const auto& [name, value] : given) {
    const auto spec = specs.find(name);
    if (spec == specs.end()) {
      return std::unexpected(std::format("unknown property '{}'", name));
    }
    if (const z13::Status valid = CheckProperty(spec->second, value); !valid) {
      return std::unexpected(std::format("'{}' {}", name, valid.error()));
    }
  }
  return {};
}

PropertyMap WithDefaults(const PropertySpecs& specs, const PropertyMap& given) {
  PropertyMap values;
  for (const auto& [name, spec] : specs) {
    values.emplace(name, spec.default_value);
  }
  for (const auto& [name, value] : given) {
    values.insert_or_assign(name, value);
  }
  return values;
}

}  // namespace z13::editor::graph
