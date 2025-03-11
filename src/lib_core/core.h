#pragma once

#include <map>
#include <string>
#include <memory>
#include <vector>

#include "common_types.h"
#include "config.h"

namespace the {

class Core {
 public:
  Core(int argc, char *argv[]);
  const Config& GetConfig() const;
  bool AddModule(ModulePtr module);
  void InitModules();
  WorldWeakPtr CreateWorld();
  WorldWeakPtr GetWorld(WorldId world_id) const;

  template <typename T, typename... Ts>
  bool CreateModule(Ts&&... params) {
    return AddModule(std::make_shared<T>(std::forward<Ts>(params)...));
  }

  void Update(float delta_time);
  int Run();
  void CleanupWorlds();

 private:
  Config config_;
  std::map<WorldId, WorldPtr> worlds_;
  WorldId new_world_id_ = 0;
  std::map<std::string, ModulePtr> modules_;
  std::vector<WorldId> worlds_to_remove_;
};

}  // namespace the
