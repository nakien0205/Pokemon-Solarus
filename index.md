# Pokemon Solarus Repository Index

> Route to the smallest relevant part of the repository.
>
> **Do not recursively read the repo.** Identify the subsystem here, then inspect only the files needed for the task.

## Must read files

1. `AGENTS.md` — Codex rules.
2. `production/session-state/active.md` — current project state.
3. This index.

## Unreal project

- `Game/PokemonSolarus.uproject` — authoritative Unreal project.
- `Game/Config/` — engine, project, input, cooking, packaging, and platform configuration.

Only read these for Unreal/project-configuration work.

## C++ game code

### `Game/Source/PokemonSolarus/`

Authoritative C++ implementation.

- `Public/Battle/` — public Battle contracts. See `Public/Battle/README.md`.
- `Private/Battle/` — Battle implementation. See `Private/Battle/README.md`.
- `Private/Tests/` — Battle automated tests. See `Private/Tests/README.md`.
- `Public/UI/` — public Battle presentation contracts. See `Public/UI/README.md`.
- `Private/UI/` — Battle presentation implementation. See `Private/UI/README.md`.
- module/target files — build and module configuration.

**Rule:** Route to the owning folder first, then use its README to narrow the search.

## Battle data

- `Game/SourceData/Battle/` — editable Battle source data, validation, and import tooling. See `Game/SourceData/Battle/README.md`.
- `Game/Content/Data/Battle/` — imported Unreal DataTables.

Prefer editable source data over generated/imported assets.

## Unreal content

- `Game/Content/UI/Battle/` — battle UI assets/widgets.
- `Game/Content/Input/` — battle input assets.
- `Game/Content/Maps/` — maps.
- `Game/Content/Art/Pokemon/` — imported Paper2D Pokemon assets.
- `Game/Content/Python/` — Unreal Editor Python tooling.

Do not broadly scan `.uasset` or `.umap` files.

## Pokemon sprite pipeline

- `Game/SourceAssets/Pokemon/` — large sprite atlas/manifest tree.
- `Game/Tools/PokemonSpriteImporter/` — sprite download/conversion/import tooling and tests.

**Do not recursively scan `Game/SourceAssets/Pokemon/`.** Open only the specific Pokemon/manifest needed.

## Design

- `design/gdd/game-concept.md` — game-wide approved direction and scope.
- `design/quick-specs/` — feature-specific approved design changes.
- `design/ux/battle-hud.md` — battle HUD design contract.
- `design/registry/` — cross-document design facts.

Read only the design document relevant to the task.

## Battle roadmap and completed mechanics

- `design/gdd/systems-index.md` — current approved playable Battle roadmap.
- `plan/battle_mechanics/` — completed Battle mechanics implementation packages for the existing foundation. Consult only when the current task depends on them. See `plan/battle_mechanics/README.md`.
- `plan/battle_mechanics/reference/modern-rules-snapshot.md` — frozen modern-Pokémon rules reference.

Use `production/session-state/active.md` to identify the current milestone and accepted design. Do not treat `plan/battle_mechanics/00-roadmap-index.md` as current project status.

## Architecture and technical docs

- `docs/registry/architecture.yaml` — compact current architecture registry.
- `docs/registry/architecture/` — accepted ADRs.
- `docs/code-file-organization.md` — code/test organization rules.
- `docs/engine-reference/` — version-pinned Unreal references.
- other `docs/*.md` — handoffs, detailed records, and historical evidence.

Prefer `architecture.yaml` before opening full ADRs. Do not load historical docs by default.

## Production

- `production/session-state/active.md` — current active project state.
- `production/qa/` — bug/QA records.
- `production/gate-checks/` — historical validation evidence.
- `production/epics/` — story/feature records.

Only read QA, gate, or epic records when the task depends on them.

## Codex and workflow

- `.codex/` — Codex config, hooks, MCP setup, and skills.
- `AGENTS.md` — Codex repository rules.
- `workflow-guide.md` — workflow routing.
- `README.md` — project overview.
- `CLAUDE.md` — Claude-oriented guidance.
- `UE.md` — project-specific Unreal reference.

Do not automatically load all root guidance or `.codex` files.

## Agent routing rules

1. Never recursively read the whole repository.
2. Start with current state and this index.
3. Search inside the owning subsystem before opening files.
4. Read only relevant plans, design docs, ADRs, and tests.
5. Prefer editable source data over generated/imported assets.
6. Do not scan sprite trees, binary Unreal assets, historical gates, or unrelated docs by default.
7. Expand context only when the current evidence is insufficient.
