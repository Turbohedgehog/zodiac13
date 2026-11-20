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
#include <charconv>
#include <format>
#include <numeric>
#include <optional>
#include <ranges>
#include <system_error>
#include <utility>

#include <flatbuffers/reflection.h>

#include <lib_core/settings/schema_attributes.h>
#include <station_properties_generated.h>

namespace z13::editor::graph {

namespace {

constexpr std::string_view kNodeTable = "node";
constexpr std::string_view kEdgeTable = "edge";
constexpr std::string_view kTitleAttribute = "title";
constexpr std::string_view kUnitAttribute = "unit";
constexpr std::string_view kListSeparator = ", ";

std::string JoinedValues(const std::vector<std::string>& values) {
  return values.empty() ? std::string {}
                        : std::accumulate(std::next(values.begin()), values.end(), values.front(),
                                          [](std::string joined, const std::string& value) {
                                            return std::move(joined) + std::string(kListSeparator) + value;
                                          });
}

std::optional<int> IntegerAttribute(const reflection::Field& field, std::string_view key) {
  const auto text = z13::schema::FieldAttribute(field, key);
  if (!text) {
    return std::nullopt;
  }
  int number {};
  const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), number);
  if (error != std::errc {} || end != text->data() + text->size()) {
    return std::nullopt;
  }
  return number;
}

bool IsInteger(reflection::BaseType type) {
  switch (type) {
    case reflection::Byte:
      [[fallthrough]];
    case reflection::UByte:
      [[fallthrough]];
    case reflection::Short:
      [[fallthrough]];
    case reflection::UShort:
      [[fallthrough]];
    case reflection::Int:
      [[fallthrough]];
    case reflection::UInt:
      return true;
    default:
      return false;
  }
}

std::expected<PropertySpec, std::string> EnumSpecOf(const reflection::Enum& type, int64_t default_value) {
  PropertySpec spec {.kind = PropertyKind::kEnum};
  for (const reflection::EnumVal* entry : *type.values()) {
    spec.values.push_back(entry->name()->str());
    if (entry->value() == default_value) {
      spec.default_value = entry->name()->str();
    }
  }
  if (!std::holds_alternative<std::string>(spec.default_value)) {
    return std::unexpected("the default is not one of the enum's values");
  }
  return spec;
}

std::expected<PropertySpec, std::string> SpecOf(const reflection::Schema& schema, const reflection::Field& field) {
  const reflection::Type& type = *field.type();
  PropertySpec spec;
  if (IsInteger(type.base_type()) && type.index() >= 0) {
    auto named = EnumSpecOf(*schema.enums()->Get(type.index()), field.default_integer());
    if (!named) {
      return named;
    }
    spec = std::move(*named);
  } else if (type.base_type() == reflection::Bool) {
    spec = {.kind = PropertyKind::kBool, .default_value = field.default_integer() != 0};
  } else if (IsInteger(type.base_type())) {
    const auto min = IntegerAttribute(field, z13::schema::kMinAttribute);
    const auto max = IntegerAttribute(field, z13::schema::kMaxAttribute);
    if (!min || !max || *min > *max) {
      return std::unexpected("an int needs min and max, min not above max");
    }
    spec = {.kind = PropertyKind::kInt,
            .min = *min,
            .max = *max,
            .default_value = static_cast<int>(field.default_integer())};
  } else {
    return std::unexpected("only an int, a bool or an enum is a property");
  }
  spec.title = std::string(z13::schema::FieldAttribute(field, kTitleAttribute).value_or(field.name()->string_view()));
  spec.unit = std::string(z13::schema::FieldAttribute(field, kUnitAttribute).value_or(""));
  if (const z13::Status valid = CheckProperty(spec, spec.default_value); !valid) {
    return std::unexpected(std::format("the default: {}", valid.error()));
  }
  return spec;
}

std::expected<PropertySpecs, std::string> SpecsOf(const reflection::Schema& schema, const reflection::Object& table,
                                                  std::string_view owner) {
  PropertySpecs specs;
  for (const reflection::Field* field : *table.fields()) {
    auto spec = SpecOf(schema, *field);
    if (!spec) {
      return std::unexpected(std::format("{} property '{}': {}", owner, field->name()->string_view(), spec.error()));
    }
    specs.emplace(field->name()->str(), std::move(*spec));
  }
  return specs;
}

std::expected<const reflection::Object*, std::string> TableOf(const reflection::Schema& schema, std::string_view name) {
  const reflection::Object* root = schema.root_table();
  const reflection::Field* field = root != nullptr ? root->fields()->LookupByKey(std::string(name).c_str()) : nullptr;
  if (field == nullptr || field->type()->base_type() != reflection::Obj) {
    return std::unexpected(std::format("the schema has no table '{}' in its root", name));
  }
  return schema.objects()->Get(field->type()->index());
}

}  // namespace

std::expected<PropertySchema, std::string> StationPropertySchema() {
  return PropertySchemaOf(*reflection::GetSchema(z13::fbs::station_properties::StationPropertiesBinarySchema::data()));
}

std::expected<PropertySchema, std::string> PropertySchemaOf(const reflection::Schema& schema) {
  const auto node_table = TableOf(schema, kNodeTable);
  if (!node_table) {
    return std::unexpected(node_table.error());
  }
  const auto edge_table = TableOf(schema, kEdgeTable);
  if (!edge_table) {
    return std::unexpected(edge_table.error());
  }
  auto node = SpecsOf(schema, **node_table, kNodeTable);
  if (!node) {
    return std::unexpected(node.error());
  }
  auto edge = SpecsOf(schema, **edge_table, kEdgeTable);
  if (!edge) {
    return std::unexpected(edge.error());
  }
  return PropertySchema {.node = std::move(*node), .edge = std::move(*edge)};
}

std::expected<PropertyValue, std::string> ParsePropertyValue(const PropertySpec& spec, std::string_view text) {
  PropertyValue value;
  switch (spec.kind) {
    case PropertyKind::kBool:
      if (text != "true" && text != "false") {
        return std::unexpected("must be true or false");
      }
      value = text == "true";
      break;
    case PropertyKind::kInt: {
      int number {};
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
      if (error != std::errc {} || end != text.data() + text.size()) {
        return std::unexpected("must be a whole number");
      }
      value = number;
      break;
    }
    case PropertyKind::kEnum:
      value = std::string(text);
      break;
  }
  if (const z13::Status valid = CheckProperty(spec, value); !valid) {
    return std::unexpected(valid.error());
  }
  return value;
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
        return std::unexpected(std::format("must be one of {}", JoinedValues(spec.values)));
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
