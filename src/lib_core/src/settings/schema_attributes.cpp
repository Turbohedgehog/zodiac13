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

#include <lib_core/settings/schema_attributes.h>
#include <lib_core/utils/status.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <set>

namespace z13::schema {

namespace {

constexpr std::string_view kLongPrefix = "--";
constexpr std::string_view kShortPrefix = "-";
constexpr char kCliNameSeparator = ',';
constexpr char kPathSeparator = '.';
// flatc stores an attribute written without a value, as in `(cli)`, as "0".
constexpr std::string_view kValuelessAttribute = "0";

std::optional<std::string_view> Attribute(const reflection::Field& field, std::string_view key) {
  const auto* attributes = field.attributes();
  if (attributes == nullptr) {
    return std::nullopt;
  }
  const reflection::KeyValue* entry = attributes->LookupByKey(std::string(key).c_str());
  if (entry == nullptr) {
    return std::nullopt;
  }
  return entry->value() != nullptr ? entry->value()->string_view() : std::string_view {};
}

std::optional<double> ParseDouble(std::string_view text) {
  double value {};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc {} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

bool IsNestedTable(const reflection::Schema& schema, const reflection::Field& field) {
  return field.type()->base_type() == reflection::Obj && !schema.objects()->Get(field.type()->index())->is_struct();
}

const reflection::Object& NestedObject(const reflection::Schema& schema, const reflection::Field& field) {
  return *schema.objects()->Get(field.type()->index());
}

bool IsUnsignedType(reflection::BaseType type) {
  return type == reflection::UByte || type == reflection::UShort || type == reflection::UInt ||
         type == reflection::ULong || type == reflection::UType;
}

constexpr size_t kBitsPerByte = 8;

// A narrow field would silently wrap a value that does not fit.
bool FitsIn(reflection::BaseType type, int64_t signed_value, uint64_t unsigned_value, bool is_unsigned) {
  const size_t bits = flatbuffers::GetTypeSize(type) * kBitsPerByte;
  if (bits >= 64) {
    return true;
  }
  if (is_unsigned) {
    return unsigned_value < (uint64_t {1} << bits);
  }
  const int64_t limit = int64_t {1} << (bits - 1);
  return signed_value >= -limit && signed_value < limit;
}

std::string JoinPath(std::string_view prefix, std::string_view name) {
  return prefix.empty() ? std::string(name) : std::format("{}{}{}", prefix, kPathSeparator, name);
}

Status ValidateRangesAt(
    const reflection::Schema& schema, const reflection::Object& object, const flatbuffers::Table& table,
    std::string_view prefix) {
  for (const reflection::Field* field : *object.fields()) {
    const std::string path = JoinPath(prefix, field->name()->string_view());
    if (IsNestedTable(schema, *field)) {
      if (const flatbuffers::Table* nested = flatbuffers::GetFieldT(table, *field)) {
        if (const auto valid = ValidateRangesAt(schema, NestedObject(schema, *field), *nested, path); !valid) {
          return valid;
        }
      }
      continue;
    }
    if (!flatbuffers::IsScalar(field->type()->base_type())) {
      continue;
    }

    const double value = flatbuffers::GetAnyFieldF(table, *field);
    // NaN is below no minimum and above no maximum, so it would pass every bound below.
    if (!std::isfinite(value)) {
      return std::unexpected(std::format("{}: {} is not a finite number", path, value));
    }
    for (const auto& [key, is_lower] : {std::pair {kMinAttribute, true}, std::pair {kMaxAttribute, false}}) {
      const auto text = Attribute(*field, key);
      if (!text) {
        continue;
      }
      const auto bound = ParseDouble(*text);
      if (!bound) {
        return std::unexpected(std::format("{}: attribute {} '{}' is not a number", path, key, *text));
      }
      if (is_lower ? value < *bound : value > *bound) {
        return std::unexpected(
            std::format("{}: {} is {} {}", path, value, is_lower ? "below the minimum" : "above the maximum", *bound));
      }
    }
  }
  return {};
}

std::expected<std::pair<std::string, std::optional<char>>, std::string> ParseCliNames(
    std::string_view spec, std::string_view field_name) {
  std::string long_name;
  std::optional<char> short_name;
  if (spec.empty()) {
    long_name = std::string(field_name);
    std::ranges::replace(long_name, '_', '-');
    return std::pair {std::move(long_name), short_name};
  }

  size_t begin = 0;
  while (begin <= spec.size()) {
    const size_t end = std::min(spec.find(kCliNameSeparator, begin), spec.size());
    std::string_view token = spec.substr(begin, end - begin);
    begin = end + 1;
    while (!token.empty() && token.front() == ' ') {
      token.remove_prefix(1);
    }
    while (!token.empty() && token.back() == ' ') {
      token.remove_suffix(1);
    }

    if (token.starts_with(kLongPrefix) && token.size() > kLongPrefix.size()) {
      if (!long_name.empty()) {
        return std::unexpected(std::format("{}: cli '{}' has two long names", field_name, spec));
      }
      long_name = std::string(token.substr(kLongPrefix.size()));
    } else if (token.starts_with(kShortPrefix) && token.size() == kShortPrefix.size() + 1 && token[1] != '-') {
      if (short_name) {
        return std::unexpected(std::format("{}: cli '{}' has two short names", field_name, spec));
      }
      short_name = token[1];
    } else {
      return std::unexpected(std::format("{}: cli name '{}' must be --long or -x", field_name, token));
    }
  }
  if (long_name.empty()) {
    return std::unexpected(std::format("{}: cli '{}' needs a long name", field_name, spec));
  }
  return std::pair {std::move(long_name), short_name};
}

std::string DescribeField(const reflection::Field& field, std::string_view path) {
  std::string description = std::string(path);
  const auto type = field.type()->base_type();
  if (flatbuffers::IsInteger(type)) {
    description += std::format(" (default {}", field.default_integer());
  } else {
    description += std::format(" (default {}", field.default_real());
  }
  const auto min = Attribute(field, kMinAttribute);
  const auto max = Attribute(field, kMaxAttribute);
  if (min || max) {
    description += std::format(", range [{}, {}]", min.value_or("-inf"), max.value_or("inf"));
  }
  return description + ")";
}

Status CollectCliOptionsAt(
    const reflection::Schema& schema, const reflection::Object& object, std::string_view prefix,
    std::vector<CliOption>& options) {
  for (const reflection::Field* field : *object.fields()) {
    const std::string path = JoinPath(prefix, field->name()->string_view());
    if (IsNestedTable(schema, *field)) {
      if (const auto nested = CollectCliOptionsAt(schema, NestedObject(schema, *field), path, options); !nested) {
        return nested;
      }
      continue;
    }
    auto spec = Attribute(*field, kCliAttribute);
    if (!spec) {
      continue;
    }
    if (*spec == kValuelessAttribute) {
      spec = std::string_view {};
    }
    const auto type = field->type()->base_type();
    if (!flatbuffers::IsScalar(type) || type == reflection::Bool) {
      return std::unexpected(std::format("{}: cli is only supported on numeric fields", path));
    }
    auto names = ParseCliNames(*spec, field->name()->string_view());
    if (!names) {
      return std::unexpected(std::format("{}: {}", path, names.error()));
    }
    options.push_back({
        .long_name = std::move(names->first),
        .short_name = names->second,
        .path = path,
        .help = DescribeField(*field, path),
    });
  }
  return {};
}

template <typename T>
std::optional<T> ParseNumber(std::string_view text) {
  T value {};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc {} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

Status ValidateRanges(
    const reflection::Schema& schema, const reflection::Object& object, const flatbuffers::Table& table) {
  return ValidateRangesAt(schema, object, table, {});
}

std::expected<std::vector<CliOption>, std::string> CollectCliOptions(
    const reflection::Schema& schema, const reflection::Object& root) {
  std::vector<CliOption> options;
  if (const auto collected = CollectCliOptionsAt(schema, root, {}, options); !collected) {
    return std::unexpected(collected.error());
  }

  std::set<std::string> long_names;
  std::set<char> short_names;
  for (const CliOption& option : options) {
    if (!long_names.insert(option.long_name).second) {
      return std::unexpected(std::format("cli name --{} is used by more than one field", option.long_name));
    }
    if (option.short_name && !short_names.insert(*option.short_name).second) {
      return std::unexpected(std::format("cli name -{} is used by more than one field", *option.short_name));
    }
  }
  return options;
}

Status SetFieldFromText(
    const reflection::Schema& schema, const reflection::Object& root, flatbuffers::Table& table,
    std::string_view path, std::string_view text) {
  const reflection::Object* object = &root;
  flatbuffers::Table* current = &table;
  std::string_view rest = path;
  while (true) {
    const size_t separator = rest.find(kPathSeparator);
    const std::string_view name = rest.substr(0, separator);
    const reflection::Field* field = object->fields()->LookupByKey(std::string(name).c_str());
    if (field == nullptr) {
      return std::unexpected(std::format("no field '{}' in '{}'", name, path));
    }

    if (separator != std::string_view::npos) {
      if (!IsNestedTable(schema, *field)) {
        return std::unexpected(std::format("'{}' in '{}' is not a table", name, path));
      }
      current = flatbuffers::GetFieldT(*current, *field);
      if (current == nullptr) {
        return std::unexpected(std::format("'{}' is not present", name));
      }
      object = &NestedObject(schema, *field);
      rest = rest.substr(separator + 1);
      continue;
    }

    const auto type = field->type()->base_type();
    if (!flatbuffers::IsScalar(type) || type == reflection::Bool) {
      return std::unexpected(std::format("'{}' is not a numeric field", path));
    }
    bool set {};
    if (flatbuffers::IsFloat(type)) {
      const auto value = ParseNumber<double>(text);
      if (!value) {
        return std::unexpected(std::format("'{}' is not a number (for {})", text, path));
      }
      set = flatbuffers::SetAnyFieldF(current, *field, *value);
    } else if (IsUnsignedType(type)) {
      const auto value = ParseNumber<uint64_t>(text);
      if (!value) {
        return std::unexpected(std::format("'{}' is not a non-negative integer (for {})", text, path));
      }
      if (!FitsIn(type, 0, *value, true)) {
        return std::unexpected(std::format("{} does not fit the field {}", text, path));
      }
      set = flatbuffers::SetAnyFieldI(current, *field, static_cast<int64_t>(*value));
    } else {
      const auto value = ParseNumber<int64_t>(text);
      if (!value) {
        return std::unexpected(std::format("'{}' is not an integer (for {})", text, path));
      }
      if (!FitsIn(type, *value, 0, false)) {
        return std::unexpected(std::format("{} does not fit the field {}", text, path));
      }
      set = flatbuffers::SetAnyFieldI(current, *field, *value);
    }
    if (!set) {
      return std::unexpected(std::format("'{}' is not stored in the buffer", path));
    }
    return {};
  }
}

}  // namespace z13::schema
