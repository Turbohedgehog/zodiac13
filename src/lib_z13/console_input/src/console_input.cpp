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


#include <console_input/console_input.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>
#include <optional>

#include <rfl/json.hpp>

namespace z13::console_input {

namespace {

bool IsSpace(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

template <class Number>
std::optional<Number> ParseNumber(std::string_view word) {
  Number value {};
  const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
  if (error != std::errc {} || end != word.data() + word.size()) {
    return std::nullopt;
  }
  return value;
}

std::expected<rfl::Generic, std::string> ValueOf(const CommandField& field, const std::string& word) {
  const auto not_a = [&](std::string_view what) {
    return std::unexpected(std::format("argument {}: '{}' is not {}", field.name, word, what));
  };
  switch (field.kind) {
    case FieldKind::kText:
      return rfl::Generic(word);
    case FieldKind::kBool:
      if (word == "true" || word == "false") {
        return rfl::Generic(word == "true");
      }
      return not_a("true or false");
    case FieldKind::kInteger:
      if (const auto value = ParseNumber<std::int64_t>(word)) {
        return rfl::Generic(*value);
      }
      return not_a("an integer");
    case FieldKind::kNumber:
      if (const auto value = ParseNumber<double>(word)) {
        return rfl::Generic(*value);
      }
      return not_a("a number");
    case FieldKind::kOther:
      break;
  }
  return std::unexpected(std::format("argument {}: a console word can't spell its type", field.name));
}

}  // namespace

std::expected<std::vector<std::string>, std::string> SplitCommandLine(std::string_view line) {
  std::vector<std::string> words;
  std::string word;
  bool in_word = false;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '\\') {
      if (++i == line.size()) {
        return std::unexpected("the line ends with a backslash");
      }
      word += line[i];
      in_word = true;
    } else if (c == '"') {
      quoted = !quoted;
      in_word = true;
    } else if (IsSpace(c) && !quoted) {
      if (in_word) {
        words.push_back(std::move(word));
        word.clear();
        in_word = false;
      }
    } else {
      word += c;
      in_word = true;
    }
  }
  if (quoted) {
    return std::unexpected("a quote is not closed");
  }
  if (in_word) {
    words.push_back(std::move(word));
  }
  return words;
}

std::expected<rfl::Generic, std::string> ArgumentsOf(std::string_view command, std::span<const CommandField> fields,
                                                     std::span<const std::string> words) {
  std::vector<bool> given(fields.size());
  rfl::Generic::Object arguments;
  std::size_t next_positional = 0;
  for (const std::string& word : words) {
    const std::size_t equals = word.find('=');
    const auto named = equals == std::string::npos
                           ? fields.end()
                           : std::ranges::find(fields, word.substr(0, equals), &CommandField::name);
    std::size_t index = 0;
    std::string value;
    if (named != fields.end()) {
      index = static_cast<std::size_t>(named - fields.begin());
      value = word.substr(equals + 1);
    } else {
      while (next_positional < given.size() && given[next_positional]) {
        ++next_positional;
      }
      if (next_positional == given.size()) {
        return std::unexpected(std::format("{} takes {} argument(s), got more", command, fields.size()));
      }
      index = next_positional;
      value = word;
    }
    const auto converted = ValueOf(fields[index], value);
    if (!converted) {
      return std::unexpected(converted.error());
    }
    arguments[fields[index].name] = *converted;
    given[index] = true;
  }
  return rfl::Generic(std::move(arguments));
}

std::string ReplyText(const rfl::Generic& reply) {
  if (const auto* text = std::get_if<std::string>(&reply.variant())) {
    return *text;
  }
  return rfl::json::write(reply);
}

Status SubmitCommandLine(flecs::world world, const CommandFilter& filter, std::string_view line,
                         CommandCallback callback) {
  const auto words = SplitCommandLine(line);
  if (!words) {
    return std::unexpected(words.error());
  }
  if (words->empty()) {
    return std::unexpected("empty line");
  }
  const CommandEntry* entry = FindCommand(world, filter, words->front());
  if (entry == nullptr) {
    return std::unexpected(std::format("unknown command '{}'", words->front()));
  }
  const auto arguments = ArgumentsOf(words->front(), entry->fields, std::span(*words).subspan(1));
  if (!arguments) {
    return std::unexpected(arguments.error());
  }
  return SubmitCommand(world, filter, words->front(), *arguments, std::move(callback));
}

}  // namespace z13::console_input
