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

#include <array>
#include <chrono>
#include <string>
#include <thread>

#include <boost/asio.hpp>
#include <flecs.h>

#include <mcp_module/mcp_module_factory.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/world/core.h>
#include <z13/components/console/assistant_chat.h>
#include <z13_settings/settings.h>

#include "../src/mcp_filter.h"
#include "../src/mcp_protocol.h"
#include "../src/mcp_system.h"

namespace z13::mcp {
namespace {

constexpr auto kWait = std::chrono::seconds(5);
constexpr auto kPoll = std::chrono::milliseconds(2);

TEST(McpProtocolTest, ParsesARequestAndDefaultsTheArguments) {
  const auto request = ParseRequest(R"({"id":7,"command":"ping"})");
  ASSERT_TRUE(request);
  EXPECT_EQ(request->id, 7);
  EXPECT_EQ(request->command, "ping");
  EXPECT_FALSE(ParseRequest(R"({"id":1})"));
  EXPECT_FALSE(ParseRequest("not json"));
}

TEST(McpProtocolTest, AReplyCarriesTheResultOrTheError) {
  EXPECT_EQ(FormatReply(3, rfl::Generic(std::string("pong"))), R"({"id":3,"ok":true,"result":"pong"})");
  EXPECT_EQ(FormatReply(4, std::unexpected("no way")), R"({"id":4,"ok":false,"error":"no way"})");
}

TEST(McpFilterTest, TheDefaultTagsAreTheDebugTag) {
  EXPECT_EQ(FilterOf(z13::McpServerSettings()).tags, (std::vector<std::string> {std::string(kDebugTag.View())}));
}

TEST(McpFilterTest, SplitsTheTagsOfTheSetting) {
  z13::McpServerSettings settings;
  settings.tags = "debug, cheat ,,admin";
  EXPECT_EQ(FilterOf(settings).tags, (std::vector<std::string> {"debug", "cheat", "admin"}));
}

// The module in a headless world, and a client of its socket on the test's thread.
class McpServerWorldTest : public testing::Test {
 protected:
  void SetUp() override {
    std::string program = "z13_test_runner";
    std::array<char*, 1> arguments = {program.data()};
    core_ = std::make_unique<z13::Core>(1, arguments.data());
    core_->RegisterModuleFactory(std::make_shared<McpModuleFactory>());
    const auto created = core_->CreateWorld();
    ASSERT_TRUE(created) << created.error();
    world_ = created->get();
  }

  // Frames until the server listens; returns its port.
  std::uint16_t Enable(bool enabled = true) {
    world_.get_mut<z13::McpServerSettings>() = [&] {
      z13::McpServerSettings settings;
      settings.enabled = enabled;
      settings.port = 0;
      return settings;
    }();
    world_.progress();
    const auto& server = world_.get<McpServerHandle>().server;
    return server ? server->Port() : 0;
  }

  std::string Ask(boost::asio::ip::tcp::socket& client, const std::string& line) {
    boost::asio::write(client, boost::asio::buffer(line + "\n"));
    return ReadLine(client);
  }

  // The next line the server sends, frame by frame.
  std::string ReadLine(boost::asio::ip::tcp::socket& client) {
    std::string reply;
    const auto deadline = std::chrono::steady_clock::now() + kWait;
    while (std::chrono::steady_clock::now() < deadline) {
      world_.progress();
      boost::system::error_code error;
      char byte {};
      while (client.read_some(boost::asio::buffer(&byte, 1), error) == 1) {
        if (byte == '\n') {
          return reply;
        }
        reply += byte;
      }
      std::this_thread::sleep_for(kPoll);
    }
    return "timeout: " + reply;
  }

  boost::asio::ip::tcp::socket Connect(std::uint16_t port) {
    boost::asio::ip::tcp::socket client(io_);
    client.connect({boost::asio::ip::address_v4::loopback(), port});
    client.non_blocking(true);
    return client;
  }

  std::unique_ptr<z13::Core> core_;
  flecs::world world_;
  boost::asio::io_context io_;
};

TEST_F(McpServerWorldTest, ADisabledServerListensOnNothing) {
  EXPECT_EQ(Enable(false), 0);
  EXPECT_FALSE(world_.get<McpServerHandle>().server);
}

TEST_F(McpServerWorldTest, PingAnswersThroughTheCommands) {
  const std::uint16_t port = Enable();
  ASSERT_NE(port, 0);
  auto client = Connect(port);
  EXPECT_EQ(Ask(client, R"({"id":1,"command":"ping"})"), R"({"id":1,"ok":true,"result":"pong"})");
}

TEST_F(McpServerWorldTest, PingAnswersWhileTheSimulationIsFrozen) {
  world_.add<z13::flecs_tools::SimulationFrozen>();
  auto client = Connect(Enable());
  EXPECT_EQ(Ask(client, R"({"id":5,"command":"ping"})"), R"({"id":5,"ok":true,"result":"pong"})");
}

TEST_F(McpServerWorldTest, ListsTheCommandsOfTheChannel) {
  auto client = Connect(Enable());
  const std::string reply = Ask(client, R"({"id":2,"command":"commands"})");
  EXPECT_NE(reply.find(R"("name":"ping")"), std::string::npos) << reply;
  EXPECT_NE(reply.find(R"("name":"commands")"), std::string::npos) << reply;
  EXPECT_NE(reply.find(R"("kind":"integer")"), std::string::npos) << reply;
}

TEST_F(McpServerWorldTest, ServerStatusCountsThePumpsOfTheGameThread) {
  auto client = Connect(Enable());
  const std::string reply = Ask(client, R"({"id":4,"command":"server_status"})");
  EXPECT_NE(reply.find(R"("ok":true)"), std::string::npos) << reply;
  EXPECT_NE(reply.find(R"("pumps":)"), std::string::npos) << reply;
  EXPECT_NE(reply.find(R"("ms_since_pump":)"), std::string::npos) << reply;
}

TEST_F(McpServerWorldTest, ASubscriberGetsWhatThePlayerSaidToTheAssistant) {
  world_.set<console::AssistantChat>({});
  auto client = Connect(Enable());
  EXPECT_EQ(Ask(client, R"({"id":1,"command":"subscribe"})"), R"({"id":1,"ok":true,"result":"subscribed"})");

  console::SayToAssistant(world_.get_mut<console::AssistantChat>(), "where am I?");

  EXPECT_EQ(ReadLine(client), R"({"event":"say","text":"where am I?"})");
  EXPECT_TRUE(world_.get<console::AssistantChat>().unsent.empty());
  EXPECT_TRUE(world_.get<console::AssistantChat>().listening);
  EXPECT_EQ(world_.get<console::AssistantChat>().lines.back().delivery, console::Delivery::kDelivered);
}

TEST(AssistantChatTest, AnAnswerMarksWhatWasDeliveredAsSeenAndLeavesTheRestAlone) {
  console::AssistantChat chat;
  console::SayToAssistant(chat, "first");
  console::SayToAssistant(chat, "second");
  console::MarkDelivered(chat, chat.lines.front().id);

  console::AssistantSays(chat, "ok");

  EXPECT_EQ(chat.lines[0].delivery, console::Delivery::kSeen);
  EXPECT_EQ(chat.lines[1].delivery, console::Delivery::kSent);
}

TEST_F(McpServerWorldTest, WhatWasSaidBeforeAnyoneListenedWaitsForTheFirstSubscriber) {
  world_.set<console::AssistantChat>({});
  auto client = Connect(Enable());
  console::SayToAssistant(world_.get_mut<console::AssistantChat>(), "anyone there?");
  world_.progress();
  ASSERT_EQ(world_.get<console::AssistantChat>().unsent.size(), 1U);
  EXPECT_FALSE(world_.get<console::AssistantChat>().listening);

  boost::asio::write(client, boost::asio::buffer(std::string(R"({"id":2,"command":"subscribe"})") + "\n"));

  EXPECT_NE(ReadLine(client).find("subscribed"), std::string::npos);
  EXPECT_EQ(ReadLine(client), R"({"event":"say","text":"anyone there?"})");
}

TEST_F(McpServerWorldTest, ASessionThatLeavesIsNotListeningAnyMore) {
  world_.set<console::AssistantChat>({});
  {
    auto client = Connect(Enable());
    ASSERT_NE(Ask(client, R"({"id":1,"command":"subscribe"})").find("subscribed"), std::string::npos);
  }
  const auto deadline = std::chrono::steady_clock::now() + kWait;
  while (world_.get<McpServerHandle>().server->Subscribers() > 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(kPoll);
  }

  world_.progress();

  EXPECT_FALSE(world_.get<console::AssistantChat>().listening);
}

TEST(McpProtocolTest, AnEventLineCarriesTheEventAndItsText) {
  EXPECT_EQ(FormatEvent("say", "hi \"you\""), R"({"event":"say","text":"hi \"you\""})");
}

TEST_F(McpServerWorldTest, RefusesWhatItDoesNotShowAndWhatIsNotARequest) {
  auto client = Connect(Enable());
  EXPECT_NE(Ask(client, R"({"id":3,"command":"nothing"})").find(R"("ok":false)"), std::string::npos);
  EXPECT_NE(Ask(client, "not json").find(R"("ok":false)"), std::string::npos);
}

}  // namespace
}  // namespace z13::mcp
