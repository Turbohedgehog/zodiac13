/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Implementation unit модуля z13.input.
//
// Конструктор копирования ActionMap определён именно здесь (неинлайново),
// чтобы boost::multi_index::multi_index_container::copy_construct_from
// инстанцировался в этом TU, где заголовки boost подключены через
// global module fragment.
//
// Причина: boost::multi_index использует boost/operators.hpp для генерации
// operator!= из operator== для bidir_node_iterator. Эти операторы находятся
// через ADL. Из-за ограничений ADL в C++20 modules (MSVC) они не видны при
// инстанцировании copy_construct_from в других модулях, которые импортируют
// z13.input. Неинлайновый конструктор переносит это инстанцирование сюда.

module;

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/container/flat_map.hpp>

module z13.input;

namespace z13::input {

ActionMap::ActionMap(const ActionMap& other) : action_map(other.action_map) {}

InputConfig::InputConfig(const InputConfig& other)
    : keycode_binding(other.keycode_binding),
      action_bindings(other.action_bindings),
      mouse_sensitivity(other.mouse_sensitivity),
      invert_x(other.invert_x),
      invert_y(other.invert_y) {}

}  // namespace z13::input