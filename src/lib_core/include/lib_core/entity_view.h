#pragma once

#include "flecs.h"

namespace z13 {

class EntityView {
 public:
  EntityView(flecs::entity& entity)
    : entity_(entity) {}

 private:
  flecs::entity& entity_;
};

}  // namespace z13
