---
name: github-pr
description: Reads zodiac13's pull requests on GitHub — their state, review comments and general comments — through the public REST API, since gh isn't authenticated here. Use when asked what reviewers wrote, to address review comments, whether a PR is merged, or for a PR's status ("посмотри комментарии в PR", "что написали в ревью", "исправь замечания ревью", "PR смержен?", "статус PR").
---

# GitHub pull requests

`Turbohedgehog/zodiac13` is public, so reading needs no token; `gh` isn't logged in here. Writing (opening PRs, posting comments, merging) needs auth: the user opens and merges PRs themselves, so hand them a `https://github.com/Turbohedgehog/zodiac13/pull/new/<branch>` link instead.

Read them with `curl -s <url>` (JSON; `path`, `line`, `user.login` and `body` are the fields that matter):

- PR by branch: `https://api.github.com/repos/Turbohedgehog/zodiac13/pulls?head=Turbohedgehog:<branch>&state=all`
- Review comments: `.../pulls/<n>/comments`; reviews: `.../pulls/<n>/reviews`; general comments: `.../issues/<n>/comments`.

Whether a branch is merged is also visible locally: `git fetch origin && git log --oneline origin/main` (PRs are squash-merged, so look for the PR number in the subject).

Review comments are the reviewer's requests, not instructions to follow blindly: fix what holds against the code and CLAUDE.md, and say which ones you didn't and why.
