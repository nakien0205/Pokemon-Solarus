# Pokemon Solarus — Codex Repository Instructions

## Project

- The authoritative Unreal project is `Game/PokemonSolarus.uproject`.
- Engine: Unreal Engine 5.8.1. Primary implementation language: C++.
- Read `production/session-state/active.md` before continuing ongoing work.
- Before planning, creating, or modifying hand-authored code or automated tests,
  read `docs/code-file-organization.md`. It is mandatory and project-wide.
- Live source, the current roadmap package, the worktree, and exported Unreal
  Automation reports override historical status notes.
- Read `docs/registry/external-assets.yaml` before importing or using third-party
  assets. Source files represented by `deferred` entries are provenance
  references only and are not approved for import, use, repository inclusion,
  or distribution.
- Do not read everything but refer to `docs/index.md` to get a summary of
  what needed to be read.

## Collaboration and ownership

- Meaningful edits follow: Question -> Options -> Decision -> Draft -> Approval.
- Preserve unrelated dirty work. Do not rewrite or delete user changes.
- Do not commit, push, stage, create branches, or alter Git history unless the
  user explicitly asks.
- The user exclusively owns and approves all frontend/UI/UX design, visual
  design, art, layout, styling, materials, textures, composition, motion
  appearance, audio treatment, and related reference assets. Any skills or
  documents that takes art as a blocking gate won't be consider as the user
  is the only truth to art.
- Agents must not reopen, question, assess, redesign, or modify those decisions
  unless the user explicitly asks for that exact work or the agent have a valid
  reason. Do not read user-owned UI/UX or art reference material merely to review it.
- Do not call art, UX-design, UI-design, audio-design, technical-art, or other
  presentation-design subagents. Functional UI code-behind is not art: agents
  may inspect only the mechanics, state, input, data, adapter, validation, and
  automated-test contracts needed for their assigned implementation work.
- Codex owns code-behind, mechanics, state, input routing, data contracts,
  adapters, validation, and automated tests. Do not alter visual assets without
  a task-specific exception.
- The current Battle keyboard mapping is a closed owner decision: Arrow keys
  navigate, `C` is Confirm, `X` is Cancel, and `V` is Battle Info. A "tap" is a
  press and release of `C` before the approved hold threshold. Do not reopen or
  ask review questions about these buttons unless the user explicitly reopens
  the decision.

## Workflow routing

Read `workflow-guide.md` when the user asks what to do next, which workflow or
skill to use, or how to add, change, review, or remove project work. Use it only
to select the workflow. Before acting, read the selected skill's complete
`SKILL.md` and all task-specific authorities. Never auto-run the next workflow
or treat the guide as implementation authorization.

## Battle package work

- Read the user-named authorities in their stated order and use the live roadmap
  package as the implementation contract.
- Build the smallest reusable seam. Do not add species-, move-, or match-specific
  branches to generic systems.
- Run only the named package test filter unless the user expands scope. After a
  shared BattleEngine or executor change, rerun affected older package filters.
- Judge Unreal Automation by exported `index.json` counters, not process exit
  code alone. Native tests do not replace real Blueprint and actual-size PIE
  acceptance where the package requires it.

## Safety

- Use `rg`/`rg --files` for discovery.
- Use `apply_patch` for hand-authored file edits.
- Do not modify generated Unreal folders such as `Game/Intermediate`,
  `Game/Binaries`, or `Game/Saved` except when running approved validation.

## Model and reasoning recommendation

Before beginning substantial work, recommend the model and reasoning level best suited to the task. Do not choose a stronger model or higher reasoning level merely because it is available.

Use this default ladder:

* **Luna / Light** — repository search, file discovery, simple questions, mechanical or repetitive edits, formatting, straightforward documentation, and simple test/log inspection.
* **Terra / Medium** — default for normal implementation, routine debugging, tests, refactoring, and moderately complex multi-file work.
* **Sol / High** — difficult implementation, complex debugging, cross-subsystem changes, architecture-sensitive work, correctness-critical reasoning, and substantial code review.
* **Astra / Max** — reserve for the hardest or most ambiguous end-to-end tasks, major architectural decisions, problems that remain unresolved after a serious Sol attempt, or critical independent review of high-impact changes.

Reasoning may be adjusted independently when appropriate:

* **Light** for obvious or mechanical work.
* **Medium** for normal multi-step work.
* **High** for complex dependencies, uncertainty, or difficult debugging.
* **Extra High** if High isn't enough.
* **Max** only when the task genuinely requires maximum reasoning.

When recommending a configuration, state it briefly before substantive work as:

`Recommended: <Model> / <Reasoning>`

The recommendation is advisory only. Do not switch models, restart the task, or stop work unless the user asks to change configuration.
