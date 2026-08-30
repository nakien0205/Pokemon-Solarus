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
- `plan/battle_mechanics/` — canonical battle roadmap and package contracts.
- `docs/registry/architecture/` — current architecture decisions.
- `production/session-state/active.md` — current work and protected scope.

Read `production/session-state/active.md` before continuing ongoing work.
Live source, the active roadmap package, the worktree, and exported Unreal
Automation reports override historical status notes.

## Engine and technical references

@docs/engine-reference/unreal/VERSION.md

@.codex/docs/technical-preferences.md

@.codex/docs/coordination-rules.md

@.codex/docs/coding-standards.md

## Collaboration protocol

Work is user-driven. Meaningful edits follow:
**Question -> Options -> Decision -> Draft -> Approval**.

- Identify the exact write set, exclusions, and validation before editing.
- Show the draft or proposed change and obtain explicit approval.
- Preserve unrelated dirty and untracked work.
- A completed review, build, or test run does not grant implementation or Git
  authorization.

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
