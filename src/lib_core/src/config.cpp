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

module;

#include <memory>
#include <ostream>

#include <boost/program_options.hpp>

module zodiac13.core;

namespace z13 {

namespace po = boost::program_options;

struct Config::Impl {
  po::options_description options_description;
  po::variables_map variables_map;

  Impl() {
    options_description.add_options()
        ("help,h", "Show help message");
  }
};

Config::Config() : impl_(std::make_unique<Impl>()) {}

Config::~Config() = default;

void Config::Clear() {
  impl_->variables_map = po::variables_map();
}

void Config::ParseCommandLineArguments(int argc, char *argv[]) {
  Clear();
  po::store(po::parse_command_line(argc, argv, impl_->options_description), impl_->variables_map);
}

bool Config::NeedShowHelp() const {
  return impl_->variables_map.count("help") > 0;
}

double Config::GetFPS() const {
  return fps_;
}

std::ostream& operator<<(std::ostream& os, const Config& config) {
  return os << config.impl_->options_description;
}

}  // namespace z13
