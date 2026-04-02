#include <z13_module/tools/z13_environment.h>

#include <z13/constants/constants.h>

#include <sago/platform_folders.h>

namespace z13::tools::environment {

std::filesystem::path GetGameDataDirectory() {
  return std::filesystem::path(sago::getDataHome()) / z13::constants::kGameName;
}

std::filesystem::path GetGameInputConfigJsonPath() {
  return GetGameDataDirectory() / "input_config.json";
}

std::filesystem::path GetGameInputConfigJsonPath2() {
  return GetGameDataDirectory() / "input_config_2.json";
}

}  // namespace z13::tools::environment
