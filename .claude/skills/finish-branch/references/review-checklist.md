# Review checklist

Findings that came up in this repo's reviews, beyond CLAUDE.md's rules themselves.

## Behaviour

- Player input: a new mechanism that can drop, delay or collapse an action (rate limits, queues, sampling once per frame) breaks CLAUDE.md's "every action reaches the simulation". Check the stall and long-frame cases.
- Phase order: moving a system to another phase can add a frame of latency or race with an unordered phase (e.g. the platform's `ReadEvents` against `ClearActionFramePhase`). Independent phases are ordered by path, not intent; give a system its own phase with an explicit `depends_on` when order matters.
- Determinism: anything that reaches state must not depend on hash-map iteration, entity ids or float accumulation; results gathered from parallel or unordered work are sorted by a key.
- Incremental against full: an incremental path (rooms, openings, block events) needs a test comparing it with a full rebuild on random edits.
- Iterators and references into a container that the same code then grows (`push_back` after `find`).

## Code

- Constants, keys and helpers defined twice (a struct built from settings in two places: give it one function).
- A comparison or equality written by hand where `operator== = default` would do.
- Tuning numbers as constants instead of settings; magic numbers.
- New headers: include what the file uses, and no more — a wide header pulled in for one type (measure with perf-measure's `header_width.py`).
- New `.cpp`/`.fbs` listed in its `CMakeLists.txt`.
- A helper the code already has, written again: grep `lib_core/utils` and the file's own anonymous namespace before adding a conversion, a sign, a clamp or a near-equality (`Sign` in `wall_seam.cpp` for `x < 0 ? -1 : 1` branches; `z13::math::ToRadians`/`ToDegrees` for degrees; `z13::math::kEpsilon`).
- A floating-point value compared with zero or another float by `==`: test the integers it comes from (`isZero()` on an integer vector), or compare within a tolerance named once.
- A `switch` that only maps one enum to a value or a list (a panel per layer, a layout field per button): a `constexpr std::array` indexed by the enumerator (with a `static_assert` on its size) or a table of member pointers reads shorter and can't lose a case to `[[fallthrough]]`. Keep the `switch` when each case does different work.
- A list of every enumerator kept next to the enum (the layers to scan, the modes to cycle): derive it from the table instead, so a new enumerator is added in one place.

- Code that runs every frame for hundreds of objects (culling, picking, per-portal or per-prop math) is written with plain structs and arrays of `float`, not Eigen expressions: a Debug build (the one we develop and measure in) keeps every temporary, and the same loop costs 3-5 times as much (`Frustum::Intersects`, `Through`). Keep Eigen at the edges of such code, and leave a comment on each such place saying why it is not Eigen (CLAUDE.md, "Code style"); a loop of this kind without one gets one.

## Tests

- New behaviour has a test that fails without the change (run it once against the old code when in doubt).
- Tests reach shipped assets through `z13_tests/shipped_station.h` and reuse the registration helpers.

## Docs

- The plan the branch implements says what was done and how it differs, with measurements (plan-and-debt skill).
- CLAUDE.md's codebase map still names the right files after moves and splits.
