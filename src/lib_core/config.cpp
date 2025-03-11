#include "config.h"
namespace the {

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

std::ostream& operator<<(std::ostream& os, const Config& person) {
  return os << person.options_description_;
}

}  // namespace the