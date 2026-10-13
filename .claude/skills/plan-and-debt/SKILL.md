---
name: plan-and-debt
description: Finds where zodiac13's plans and technical debt live in docs/, works out what is done and what comes next, proposes the next branches, and records a finished stage or a resolved debt in the plan. Use when asked what's next, what the plan or the debts are, what's left, or to update a plan after a branch ("что дальше по плану", "какие долги", "посмотрим на план", "что осталось", "обнови план", "отметь в плане").
---

# Plans and debt

The plans are Russian Markdown documents in `docs/`, each the source of truth for its area; references/plans.md says which is which. The current status (main's commit, open PRs, the user's latest choices) is in the status memory, but the plans decide what a stage contains.

## What's next

1. Read the plans the question concerns — for station work `station-systems-plan.md`, `performance-research-plan.md`, `multi-grid-plan.md`, `oblique-walls-plan.md`, `dynamic-objects-plan.md`; for networking `client-server-plan.md` ("Технический долг"), `action-delivery-plan.md`, `network-scaling-plan.md`, `server-plan.md`.
2. In each "Порядок работ", a stage that is done carries a note under it starting with "Сделано" (with the branch and how it differs from the plan); the stages without one are open. The dependency line after the list ("Этапы 2 и 3 независимы; ...") says what can start now.
3. Debts: "Технический долг" in `client-server-plan.md` collects them across areas (resolved ones are marked "— решено (`branch`)"); `performance-research-plan.md` has the table of long operations and "Решения под вопросом"; "Открытые вопросы" in each plan need a user decision before their stage.
4. Answer with a short grouped list (area: branch, size estimate, what it unblocks) and one recommended order with the reason. Filter by what the user asked (e.g. "не связанные с рендером").

## Recording progress (in the branch that does the work)

- A finished stage: under its item, "Сделано (`branch`). Отличия: ..." — what differs from the plan, the numbers measured (with conditions, see perf-measure), and what is left for later.
- A resolved debt: rewrite the bullet as "**...** — решено (`branch`). Было: ... Теперь: ..." in a few sentences.
- A new debt found along the way goes into "Технический долг" (or the area's plan) with why it matters to the player.
- Keep the plans in Russian, as they are.

## References

- references/plans.md — every document in `docs/`, what it covers and where its stage list and debts are.
- references/modules.md — the station building module's systems in detail (brush, blueprint scenes, build and destroy flow).
