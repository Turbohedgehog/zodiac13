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

#include <gtest/gtest.h>

#include <z13_launcher/module_list.h>

namespace z13 {
namespace {

constexpr char kSampleYaml[] = R"(
modules:
  - modules/z13_module/z13_module
  - path: modules/raylib/raylib_module
  - modules/test_dll/test_dll_module
)";

TEST(ParseModuleListTest, ScalarAndMapEntriesAreBothRead) {
  const auto modules = ParseModuleList(kSampleYaml);

  ASSERT_TRUE(modules.has_value());
  ASSERT_EQ(modules->size(), 3u);
  EXPECT_EQ((*modules)[0], "modules/z13_module/z13_module");
  EXPECT_EQ((*modules)[1], "modules/raylib/raylib_module");
  EXPECT_EQ((*modules)[2], "modules/test_dll/test_dll_module");
}

TEST(ParseModuleListTest, MalformedEntryIsSkippedNotFatal) {
  constexpr char kYaml[] = R"(
modules:
  - modules/a
  - foo: bar
  - modules/b
)";
  const auto modules = ParseModuleList(kYaml);

  ASSERT_TRUE(modules.has_value());
  ASSERT_EQ(modules->size(), 2u);
  EXPECT_EQ((*modules)[0], "modules/a");
  EXPECT_EQ((*modules)[1], "modules/b");
}

TEST(ParseModuleListTest, MissingModulesSequenceReturnsError) {
  EXPECT_FALSE(ParseModuleList("not_modules: []").has_value());
}

TEST(ParseModuleListTest, MalformedYamlReturnsError) {
  EXPECT_FALSE(ParseModuleList("modules: [this is not: valid: yaml:").has_value());
}

}  // namespace
}  // namespace z13
