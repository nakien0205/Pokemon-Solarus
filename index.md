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

- `Public/Battle/` — public battle contracts, types, setup, snapshots, decisions, rules, and reusable APIs.
- `Private/Battle/` — battle implementation.
  - `BattleEngine*` — orchestration.
  - `BattleEffectExecutor*` — reusable effect execution.
  - Other files cover targeting, damage, status, abilities, items, switching, encounters, capture, replay, etc.
- `Private/Tests/` — battle automated tests.
- `Public/UI/`, `Private/UI/` — battle HUD, command UI, controller, GameMode, and presentation.
- module/target files — build and module configuration.

**Rule:** Search the relevant subsystem first. Do not scan all Battle or Test files.

## Battle data

- `Game/SourceData/Battle/Initial/` — authoritative editable battle JSON.
- `Game/SourceData/Battle/` — validation and DataTable import tooling.
- `Game/Content/Data/Battle/` — imported Unreal DataTables.

Prefer `Game/SourceData/` for reasoning and edits. Do not inspect generated/imported DataTables unless the task concerns Unreal import/runtime assets.

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

## Battle plans

- `plan/battle_mechanics/00-roadmap-index.md` — high-level battle roadmap/status.
- `02-*` — core contracts/events/RNG.
- `03-*` — stats/types/moves/data.
- `04-*` — state/snapshots/decisions.
- `05-*` — actions/order/targeting.
- `06-*` — hit/damage/effects/outcomes.
- `07-*` — parties/switching/replacements.
- `08-*` — status/volatile/field/side conditions.
- `09-*` — abilities/items.
- `10-*` — encounters/capture/escape/partner.
- `11-*` — canonical proof content.
- `12-*` — integration/release gate.
- `reference/modern-rules-snapshot.md` — large frozen modern-Pokemon rules reference.

Do not read packages sequentially. Read only the package that owns the task.

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
