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

#include <ostream>
#include <boost/program_options.hpp>

namespace z13 {

class Config {
 public:
  Config();
  void Clear();

  void ParseCommandLineArguments(int argc, char *argv[]);
  boost::program_options::options_description& GetOptionsDescription();
  bool NeedShowHelp() const;
  friend std::ostream& operator<<(std::ostream& os, const Config& person);
  double GetFPS() const;

 private:
  boost::program_options::options_description options_description_;
  boost::program_options::variables_map variables_map_;
  double fps_ = 60.f;
};

}  // namespace z13
