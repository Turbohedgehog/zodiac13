// z13.core.config module partition unit.
// Перенесён из include/lib_core/config.h.

module;

#include <ostream>

#include <boost/program_options.hpp>

export module z13.core.config;

export namespace z13 {

class Config {
 public:
  Config();
  void Clear();

  void ParseCommandLineArguments(int argc, char* argv[]);
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