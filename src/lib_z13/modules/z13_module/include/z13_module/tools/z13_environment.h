#pragma once

#include <filesystem>

namespace z13::tools::environment {

std::filesystem::path GetGameDataDirectory();
std::filesystem::path GetGameInputConfigJsonPath();
std::filesystem::path GetGameInputConfigJsonPath2();

} // namespace z13::environment