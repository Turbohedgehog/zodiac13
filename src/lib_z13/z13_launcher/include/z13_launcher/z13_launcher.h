#pragma once

#include <optional>
#include <utility>

#include <lib_core/utils/asset_source.h>

namespace z13 {

class Zodiac13Launcher {
 public:
  // Reads the game's files from `source` instead of next to the executable (Android: the APK).
  void UseAssets(AssetSource source) { assets_ = std::move(source); }

  int Run(int argc, char *argv[]);

 private:
  std::optional<AssetSource> assets_;
};

}  // namespace z13
