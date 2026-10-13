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

## Tests

- New behaviour has a test that fails without the change (run it once against the old code when in doubt).
- Tests reach shipped assets through `z13_tests/shipped_station.h` and reuse the registration helpers.

## Docs

- The plan the branch implements says what was done and how it differs, with measurements (plan-and-debt skill).
- CLAUDE.md's codebase map still names the right files after moves and splits.
