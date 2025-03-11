#pragma once

#include <ostream>
#include <boost/program_options.hpp>

namespace the {

class Config {
 public:
  Config();
  void Clear();

  void ParseCommandLineArguments(int argc, char *argv[]);
  boost::program_options::options_description& GetOptionsDescription();
  bool NeedShowHelp() const;
  friend std::ostream& operator<<(std::ostream& os, const Config& person);

 private:
  boost::program_options::options_description options_description_;
  boost::program_options::variables_map variables_map_;
};

}  // namespace the
