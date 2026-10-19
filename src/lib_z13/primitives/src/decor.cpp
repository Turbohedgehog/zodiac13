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

#include <primitives/decor.h>

#include <format>

#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>

#include <decor_generated.h>

namespace z13::building::primitives {

std::expected<Decor, std::string> ParseDecor(std::string_view json) {
  flatbuffers::Parser parser;
  if (!parser.Deserialize(reflection::GetSchema(fbs::station::DecorBinarySchema::data()))) {
    return std::unexpected("decor: failed to deserialize the binary schema");
  }
  if (!parser.Parse(std::string(json).c_str())) {
    return std::unexpected(std::format("decor: {}", parser.error_));
  }
  fbs::station::DecorT source;
  fbs::station::GetDecor(parser.builder_.GetBufferPointer())->UnPackTo(&source);

  Decor decor {.replaces = source.replaces};
  decor.props.reserve(source.props.size());
  for (size_t i = 0; i < source.props.size(); ++i) {
    const fbs::station::DecorPropT& entry = *source.props[i];
    if (entry.model.empty() || !entry.position) {
      return std::unexpected(std::format("decor: prop {}: no model or position", i));
    }
    decor.props.push_back({.model = entry.model,
                     .position = {entry.position->x(), entry.position->y(), entry.position->z()},
                           .yaw = entry.yaw,
                           .pitch = entry.pitch});
  }
  return decor;
}

}  // namespace z13::building::primitives
