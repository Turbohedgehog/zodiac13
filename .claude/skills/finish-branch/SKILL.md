---
name: finish-branch
description: Reviews a branch and closes it out — a code review that always deletes or shortens redundant comments itself, CRLF and size checks, the single build and test run, commit, push and PR link — and handles what follows: conflicts after the previous branch was squash-merged, updating main once a PR is merged. Use when asked for a code review, to trim redundant comments, to commit or push, when a PR has conflicts, or when told a PR was merged ("код ревью", "проведи ревью", "удали избыточные комментарии", "закоммить", "запушь", "у PR конфликт", "смержил", "обнови main").
---

# Finish a branch

The rules a review checks against are in CLAUDE.md ("Code style", "Comments", "Flecs components", "Branch size", "Code review"); this skill is the procedure.

## Code review

Run all of it on every review, before any build (a wide rebuild takes about an hour in PRoot):

1. `git diff main --stat` — size against the ~1000-line rule; flag an overrun (station-plan branches may exceed it: mention, don't treat as a defect). Look for unexpected whole-file diffs: usually a CRLF file rewritten with LF (step 4).
2. Read the whole diff for bugs and CLAUDE.md rule violations. Common findings in this repo are listed in references/review-checklist.md — go through it.
3. Comments — **always fixed in the same pass, without being asked** (user decision 2026-10-13): `git diff main -U0 | grep -E '^(\+\+\+ |\+\s*//)'` lists every comment line the branch adds under its file. Delete each one that restates a name or the code, repeats another comment or a CLAUDE.md convention, or is commented-out code; shorten each one longer than the "Comments" section allows. Moved code counts as added: review moved comments too. Keep the ones that say why, or what the code can't.
4. `python3 .claude/skills/finish-branch/scripts/crlf_restore.py --check [BASE]` lists files whose CRLF line endings the branch lost; run it without `--check` to restore them.
5. Fix what the review found, then report the findings and what was changed.

A review of an already-built branch whose fixes touch only comments needs no rebuild.

## Verify, commit, push

1. One build and test run for the whole change set, then the CI checks (build-and-test skill).
2. Commit with a short message (a subject plus a sentence or two of why) ending in the attribution lines the session gives. Never put session or tooling ids in it.
3. `git push -u origin <branch>`; give the user the `https://github.com/Turbohedgehog/zodiac13/pull/new/<branch>` link — the user opens PRs and merges them.
4. Update the plan the branch implements (plan-and-debt skill) in the same branch, before the build.

## When the previous branch was squash-merged

A branch stacked on another keeps the other's original commits, so after the user squash-merges the first, the second conflicts with main:

1. `git fetch origin && git merge --no-edit origin/main` on the branch (merge, not rebase: no force push needed).
2. Code conflicts: the branch's side is usually the newer one (it already contains the first branch plus later fixes) — `git checkout --ours <files>`, then check that main's other changes survived. Docs conflicts: combine both sides by hand.
3. `git diff --cached HEAD --stat -- src` empty means the code didn't change and no rebuild is needed; otherwise run the tests once.
4. `git merge-tree --write-tree origin/main HEAD` confirms the branch now merges cleanly; commit and push.

## After the user merges

`git checkout main && git pull`, delete the local branch (`git branch -d`), update the status memory, and offer the next step (plan-and-debt skill).

## Edits across many files

For a sweep (renames, include changes, comment trimming) take the whole diff or grep once, read the targets together, then edit straight through the list. When a script rewrites files, read and write them as bytes so CRLF files stay CRLF.
