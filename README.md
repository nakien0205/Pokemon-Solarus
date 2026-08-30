# Pokémon Solarus

Pokémon Solarus is an Unreal Engine project whose current milestone is a
deterministic, data-driven, single-player Pokémon battle. The battle runtime is
implemented primarily in C++, with Unreal assets and Blueprints used for editor
integration and presentation.

## Project

- **Engine:** Unreal Engine 5.8.1, changelist 56057345
- **Authoritative project:** `Game/PokemonSolarus.uproject`
- **Primary language:** C++
- **Target platform:** Windows PC

Current package status and the next approved boundary are recorded in
[`production/session-state/active.md`](production/session-state/active.md).

## Repository map

- `Game/` — Unreal project, C++ source, tests, source data, configuration, and
  content assets.
- `plan/battle_mechanics/` — canonical battle roadmap and package contracts.
- `docs/registry/architecture/` — current Architecture Decision Records.
- `docs/engine-reference/unreal/` — project-scoped Unreal Engine 5.8.1 notes.
- `design/` — game-design documents and approved quick specifications.
- `production/` — current state, evidence gates, stories, and QA records.
- `.codex/` — local project workflows, skills, rules, and supporting guidance.

## Before working

Read the authorities relevant to the task:

1. [`AGENTS.md`](AGENTS.md) for repository rules and approval boundaries.
2. [`CLAUDE.md`](CLAUDE.md) for project structure and collaboration rules.
3. [`production/session-state/active.md`](production/session-state/active.md)
   for current work and protected scope.
4. [`plan/battle_mechanics/00-roadmap-index.md`](plan/battle_mechanics/00-roadmap-index.md)
   for battle-package order and status.
5. [`docs/registry/architecture/`](docs/registry/architecture/) for current
   architecture decisions.
6. [`docs/code-file-organization.md`](docs/code-file-organization.md) before
   planning or changing hand-authored code or automated tests.
7. [`docs/registry/external-assets.yaml`](docs/registry/external-assets.yaml)
   before importing or using a third-party asset.

Live source, the current roadmap package, the worktree, and fresh exported
Unreal Automation reports override historical status notes.

## Opening the project

Use the pinned Unreal Engine 5.8.1 installation and open:

```text
Game/PokemonSolarus.uproject
```

Do not silently retarget the project to another engine version. Build and test
commands are package-specific; use the validation contract in the active
roadmap package rather than a generic command copied from older evidence.

## Collaboration and ownership

- Meaningful edits follow **Question -> Options -> Decision -> Draft ->
  Approval**.
- Do not stage, commit, push, create branches, or alter Git history without
  explicit task-specific authorization.
- Preserve unrelated dirty and untracked work.
- The project owner controls visual layout, styling, art, materials, textures,
  composition, and motion appearance.
- Codex owns code-behind, mechanics, state, input routing, data contracts,
  adapters, validation, and automated tests unless a task grants an exception.
