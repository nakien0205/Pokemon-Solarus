# Pokémon Solarus — Repository Instructions

Pokémon Solarus is an Unreal Engine battle project. The authoritative project is
`Game/PokemonSolarus.uproject`.

## Technology stack

- **Engine:** Unreal Engine 5.8.1, changelist 56057345
- **Primary language:** C++
- **Editor integration:** Unreal assets and Blueprint
- **Build system:** Unreal Build Tool
- **Version control:** Existing Git repository. Do not stage, unstage, commit,
  push, create branches, or alter history without explicit task-specific user
  instruction.

## Project structure

- `Game/Source/PokemonSolarus/` — production C++ and native Automation tests.
- `Game/SourceData/Battle/` — authored battle source data and import tooling.
- `Game/Content/` — Unreal assets and user-owned visual content.
- `plan/battle_mechanics/` — detailed Battle mechanics packages and completed implementation requirements; consult only when the current task depends on them.
- `docs/registry/architecture/` — current architecture decisions.
- `production/session-state/active.md` — current work and protected scope.

Read `production/session-state/active.md` before continuing ongoing work.
Live source, the active milestone and its current accepted authority, the
worktree, and exported Unreal Automation reports override historical status notes.

## Engine and technical references

@docs/engine-reference/unreal/VERSION.md

@.codex/docs/technical-preferences.md

@.codex/docs/coordination-rules.md

@.codex/docs/coding-standards.md

## Collaboration protocol

Work is user-driven. Follow the development mode and active milestone recorded
in `production/session-state/active.md`.

For ordinary `PLAYABLE` work, use:

**Goal -> Decision if needed -> Implement -> Focused Validation -> PIE -> Accept or Fix**

- If an accepted design already settles the behavior, do not reopen it.
- If the user directly requests implementation of an already-approved bounded
  scope, that request authorizes implementation of that scope.
- Do not require a separate implementation-draft or independent-approval cycle
  unless the active authority explicitly requires one or a genuine architecture
  trigger is discovered.
- Identify the expected write set, exclusions, and relevant validation before
  editing, but keep this proportional to the task.
- Preserve unrelated dirty and untracked work.
- Reviews, builds, and tests provide evidence; they do not authorize Git
  operations.
- Do not stage, commit, push, branch, reset, checkout, or alter Git history
  without explicit task-specific user authorization.

## Context discipline

Do not read the whole repository before beginning work.

Start with:

1. `AGENTS.md`
2. `production/session-state/active.md`
3. `index.md`

Historical plans, ADRs, reports, and evidence are read only when the current
task depends on them.

## Mandatory code-file organization

Before planning, creating, or modifying hand-authored code or automated tests,
read and follow:

@docs/code-file-organization.md

## External assets

Before importing or using third-party assets, read
`docs/registry/external-assets.yaml`. A `deferred` entry is provenance-only and
does not approve import, repository inclusion, use, or distribution.

## Context management

@.codex/docs/context-management.md

## Ownership

- The user owns frontend and UI/UX visual design and assets, including layout,
  styling, art, materials, textures, widget or scene composition, and motion
  appearance.
- Codex owns mechanics and code-behind, including state, input routing, data
  contracts, adapters, validation, and automated tests.
- Codex may expose bindings and events or inspect frontend work, but must not
  create or modify visual decisions or assets without a task-specific
  exception.
- After completing a task, state the concrete next step.
