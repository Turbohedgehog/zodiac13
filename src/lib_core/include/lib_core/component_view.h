#pragma once

#include "flecs.h"

#include <lib_core/entity_view.h>

namespace z13 {

template <typename T>
class ComponentView {
 public:
  ComponentView(EntityView entity_view, T& component)
    : entity_view_(entity_view)
    , component_(component) {}

  T& operator*() { return component_; }
  const T& operator*() const { return component_; }

  T* operator->() { return &component_; }
  const T* operator->() const { return &component_; }

 private:
  EntityView entity_view_;
  T& component_;
};

}  // namespace z13
