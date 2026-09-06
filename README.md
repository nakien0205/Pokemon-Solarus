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
- `plan/battle_mechanics/` — detailed Battle mechanics packages and completed implementation requirements; consult only when the current task depends on them.
- `docs/registry/architecture/` — current Architecture Decision Records.
- `docs/engine-reference/unreal/` — project-scoped Unreal Engine 5.8.1 notes.
- `design/` — game-design documents and approved quick specifications.
- `production/` — current state, evidence gates, stories, and QA records.
- `.codex/` — local project workflows, skills, rules, and supporting guidance.

## Before working

Use the smallest relevant context set for the current task:

1. [`AGENTS.md`](AGENTS.md) for repository rules and development-mode behavior.
2. [`production/session-state/active.md`](production/session-state/active.md)
   for the current playable milestone, scope, exclusions, and next action.
3. [`index.md`](index.md) to locate the files and authorities relevant to the
   task.

Do not read the entire repository, all Battle plans, all ADRs, or historical
evidence by default.

Read [`docs/code-file-organization.md`](docs/code-file-organization.md) before
planning or changing hand-authored code or automated tests.

Read [`docs/registry/external-assets.yaml`](docs/registry/external-assets.yaml)
before importing or using third-party assets.

Live source, the active milestone, the worktree, and fresh relevant evidence
override historical status notes.

## Opening the project

Use the pinned Unreal Engine 5.8.1 installation and open:

```text
Game/PokemonSolarus.uproject
```

Do not silently retarget the project to another engine version. Build and test
commands are package-specific; use the validation contract in the active
roadmap package rather than a generic command copied from older evidence.

## Collaboration and ownership

- Solarus uses `SPIKE`, `PLAYABLE`, and `PRODUCTION` development modes.
- `PLAYABLE` is the default unless the active project state says otherwise.
- Ordinary playable work follows:
  **Goal -> Decision if needed -> Implement -> Focused Validation -> PIE ->
  Accept or Fix**.
- Future requirements may influence clean extension boundaries but must not
  create present work.
- Do not stage, commit, push, create branches, reset, checkout, or alter Git
  history without explicit task-specific authorization.
- Preserve unrelated dirty and untracked work.
- The project owner exclusively controls visual/UI/UX design, styling, art,
  materials, textures, composition, motion appearance, and audio treatment.
- Codex owns functional code-behind, mechanics, state, input routing, data
  contracts, adapters, validation, and automated tests unless a task grants an
  exception.
