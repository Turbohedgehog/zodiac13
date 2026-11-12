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


#include "mcp_session.h"

#include <istream>
#include <utility>

namespace z13::mcp {

namespace {

// A longer line is not a request: the connection is dropped instead of buffering it.
constexpr std::size_t kMaxLineBytes = 64 * 1024;

}  // namespace

McpSession::McpSession(boost::asio::ip::tcp::socket socket, std::uint64_t id, LineHandler on_line,
                           ClosedHandler on_closed)
    : socket_(std::move(socket)),
      id_(id),
      on_line_(std::move(on_line)),
      on_closed_(std::move(on_closed)),
      input_(kMaxLineBytes) {}

void McpSession::Start() {
  ReadLine();
}

void McpSession::ReadLine() {
  boost::asio::async_read_until(socket_, input_, '\n',
                                [self = shared_from_this()](const boost::system::error_code& error, std::size_t) {
                                  if (error) {
                                    self->Close();
                                    return;
                                  }
                                  std::istream stream(&self->input_);
                                  std::string line;
                                  std::getline(stream, line);
                                  if (!line.empty() && line.back() == '\r') {
                                    line.pop_back();
                                  }
                                  if (!line.empty()) {
                                    self->on_line_(self->id_, std::move(line));
                                  }
                                  self->ReadLine();
                                });
}

void McpSession::Write(std::string line) {
  if (closed_) {
    return;
  }
  line += '\n';
  outbox_.push_back(std::move(line));
  if (!writing_) {
    WriteNext();
  }
}

void McpSession::WriteNext() {
  if (outbox_.empty()) {
    writing_ = false;
    return;
  }
  writing_ = true;
  boost::asio::async_write(socket_, boost::asio::buffer(outbox_.front()),
                           [self = shared_from_this()](const boost::system::error_code& error, std::size_t) {
                             if (error) {
                               self->Close();
                               return;
                             }
                             self->outbox_.pop_front();
                             self->WriteNext();
                           });
}

void McpSession::Close() {
  if (closed_) {
    return;
  }
  closed_ = true;
  boost::system::error_code ignored;
  socket_.close(ignored);
  on_closed_(id_);
}

}  // namespace z13::mcp
