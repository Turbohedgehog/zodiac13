#pragma once

#include <filesystem>

namespace z13::tools::environment {

std::filesystem::path GetGameDataDirectory();
std::filesystem::path GetGameInputConfigJsonPath();

} // namespace z13::environment