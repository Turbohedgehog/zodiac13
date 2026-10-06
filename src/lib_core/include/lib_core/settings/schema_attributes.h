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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/reflection.h>

#include <lib_core/utils/status.h>

// Field attributes (fbs/attributes.fbs) read from a binary schema, so any table is checked and
// overridden the same way:
//   (min: 1, max: 1000)  inclusive bounds of a scalar field, checked by ValidateRanges;
//   (cli)                a command-line option named after the field (max_late_ticks -> --max-late-ticks);
//   (cli: "-f,--fps")    explicit names, like argparse: one long form and an optional one-letter short form.
// Nested tables are walked recursively; a field is addressed by its dotted path ("net.max_late_ticks").
namespace z13::schema {

inline constexpr std::string_view kMinAttribute = "min";
inline constexpr std::string_view kMaxAttribute = "max";
inline constexpr std::string_view kCliAttribute = "cli";

Status ValidateRanges(
    const reflection::Schema& schema, const reflection::Object& object, const flatbuffers::Table& table);

struct CliOption {
  std::string long_name;
  std::optional<char> short_name;
  std::string path;
  std::string help;
};

std::expected<std::vector<CliOption>, std::string> CollectCliOptions(
    const reflection::Schema& schema, const reflection::Object& root);

// Writes `text` into the numeric field at `path`. The field must be present in the buffer
// (build it with ForceDefaults), since a scalar equal to its default is otherwise omitted.
Status SetFieldFromText(
    const reflection::Schema& schema, const reflection::Object& root, flatbuffers::Table& table,
    std::string_view path, std::string_view text);

}  // namespace z13::schema
