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

#include <memory>
#include <vector>

#include <z13/components/utilities.h>

#include "../../src/session_settings.h"

namespace z13::net {
namespace {

using z13::utility::UtilityModule;

const std::vector<UtilityModule> kServer = {{.name = "conduit", .version = 1}, {.name = "power", .version = 2}};

TEST(UtilityModulesTest, TheSameSetFromTheWireMatches) {
  EXPECT_EQ(UtilityModulesMismatch(kServer, ToWire(kServer)), std::nullopt);
  const std::vector<UtilityModule> none;
  EXPECT_EQ(UtilityModulesMismatch(none, ToWire(none)), std::nullopt);
}

TEST(UtilityModulesTest, ADifferentVersionNamesTheModuleAndBothVersions) {
  std::vector<UtilityModule> client = kServer;
  client[1].version = 3;

  const auto mismatch = UtilityModulesMismatch(kServer, ToWire(client));

  ASSERT_TRUE(mismatch.has_value());
  EXPECT_NE(mismatch->find("'power' is version 3 on the client and 2 on the server"), std::string::npos) << *mismatch;
}

TEST(UtilityModulesTest, AMissingModuleIsNamedOnWhicheverSideLacksIt) {
  const auto client_lacks = UtilityModulesMismatch(kServer, ToWire(std::vector {kServer[0]}));
  ASSERT_TRUE(client_lacks.has_value());
  EXPECT_NE(client_lacks->find("the client lacks 'power'"), std::string::npos) << *client_lacks;

  const auto server_lacks = UtilityModulesMismatch(std::vector {kServer[0]}, ToWire(kServer));
  ASSERT_TRUE(server_lacks.has_value());
  EXPECT_NE(server_lacks->find("the server lacks 'power'"), std::string::npos) << *server_lacks;
}

TEST(UtilityModulesTest, TheOrderOfTheListsDoesNotMatter) {
  const std::vector<UtilityModule> reversed = {kServer[1], kServer[0]};

  EXPECT_EQ(UtilityModulesMismatch(kServer, ToWire(reversed)), std::nullopt);
}

}  // namespace
}  // namespace z13::net
