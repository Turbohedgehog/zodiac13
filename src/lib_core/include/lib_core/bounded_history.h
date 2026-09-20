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

#pragma once

#include <algorithm>
#include <deque>
#include <iterator>
#include <utility>
#include <vector>

namespace z13 {

// Bounded, oldest-first history: Push() appends, PruneOlderThan() drops entries from
// the front while the predicate holds. A deque-backed alternative to a hand-rolled ring
// buffer -- no index wraparound to get wrong, still O(1) push/pop at each end. Callers
// decide what "too old" means (elapsed ticks, a count, ...) via the predicate.
template <typename T>
class BoundedHistory {
 public:
  void Push(T entry) { entries_.push_back(std::move(entry)); }

  template <typename Predicate>
  void PruneOlderThan(Predicate should_prune) {
    while (!entries_.empty() && should_prune(entries_.front())) {
      entries_.pop_front();
    }
  }

  // Merges already-sorted `incoming` into this history in place, keeping it sorted by
  // `less` -- unlike Push(), for entries that don't all belong at the end.
  template <typename Compare>
  void MergeSorted(std::vector<T> incoming, Compare less) {
    const auto original_end = entries_.insert(
        entries_.end(), std::make_move_iterator(incoming.begin()), std::make_move_iterator(incoming.end()));
    std::inplace_merge(entries_.begin(), original_end, entries_.end(), less);
  }

  const std::deque<T>& Entries() const { return entries_; }
  bool Empty() const { return entries_.empty(); }

 private:
  std::deque<T> entries_;
};

}  // namespace z13
