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

#include <lib_core/config.h>

namespace z13 {

namespace po = boost::program_options;

Config::Config() {
  options_description_.add_options()
      ("help,h", "Show help message");
}

void Config::Clear() {
  variables_map_ = boost::program_options::variables_map();
}

boost::program_options::options_description& Config::GetOptionsDescription() {
  return options_description_;
}

void Config::ParseCommandLineArguments(int argc, char *argv[]) {
  Clear();
  po::store(po::parse_command_line(argc, argv, options_description_), variables_map_);
}

bool Config::NeedShowHelp() const {
  return variables_map_.count("help") > 0;
}

double Config::GetFPS() const {
  return fps_;
}

std::ostream& operator<<(std::ostream& os, const Config& person) {
  return os << person.options_description_;
}

}  // namespace z13