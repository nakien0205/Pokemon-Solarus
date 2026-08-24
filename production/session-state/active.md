<!-- STATUS -->
Epic: Battle System
Feature: Production Battle Runtime and HUD
Task: Review current runtime changes and complete manual HUD acceptance
<!-- /STATUS -->

# Active Project State — 2026-08-24

## Current work

- ADR-0001, `docs/architecture/adr-0001-data-driven-battle-runtime-and-fail-closed-hud.md`, is accepted and has an uncommitted implementation in the worktree.
- The implementation moves the initial Battle runtime to cooked DataTables, injects an `IBattleRuntimeSource`, builds an atomic `FBattleHUDDisplayState`, and makes presentation caching HUD-generation aware.
- The Battle HUD visual assets and Blueprint edits are user-owned work. Codex must not change their appearance without a new task-specific exception.
- The broader battle roadmap still records B00 through C08C complete and C09A as the next dependency-clear mechanics package. Do not begin C09A until the current production-runtime/HUD changes are reviewed and accepted.

## Verified evidence

- `Game/Saved/Automation/ADR0001-DataSource-20260824-1000/report/index.json`: 2 succeeded, 0 failed, 0 not run, 0 in process.
- `Game/Saved/Automation/ADR0001-BattleUI-Rerun-20260824-1004/report/index.json`: 20 succeeded, 0 failed, 0 not run, 0 in process. This supersedes the earlier 18/20 run.
- `Game/Saved/Automation/ADR0001-C02BAdapter-20260824-1005/report/index.json`: 2 succeeded, 0 failed, 0 not run, 0 in process.
- `Game/Saved/Packaging/ADR0001-Win64-20260824-1013/PackagedSmoke.log`: the staged Windows build mounted its containers, loaded `FoundationMap` with `BattleGameMode`, and exited with status 0 without fatal/error entries.
- Actual-size visual acceptance at 1920x1080 and 1280x720 is not verified by this evidence and remains a manual Blueprint/PIE gate.

## Working-tree scope to preserve

- User-owned visual work: Battle HUD/command UI Blueprints, Battle menu textures, and their `.uasset` imports.
- Runtime/code-behind work: Battle GameMode, HUD, controller, health panel, presentation adapter, runtime source/data-row contracts, and focused tests.
- Data/config/docs work: `Game/Config/DefaultGame.ini`, `/Game/Data/Battle/Initial`, `Game/SourceData/Battle/Initial`, ADR-0001, and `docs/registry/architecture.yaml`.
- Unrelated existing changes include `CLAUDE.md` and deletion of `UE.md`; preserve them and do not fold them into a different task without explicit approval.

## Codex migration

- Repository instructions now live in `AGENTS.md` and are selected for tracking by `.gitignore`.
- Active hooks live in `.codex/hooks.json`, use native PowerShell through `.codex/hooks/project-hooks.ps1`, and are selected for tracking by `.gitignore`.
- The old ignored Claude-style `.codex/settings.json`, shell hooks, and custom status-line script are obsolete and are not part of the active Codex contract.
- A new Codex session must review/trust the repository hooks through `/hooks` before relying on automatic execution.

## Next

1. Review the uncommitted ADR-0001 runtime/code-behind changes against the accepted ADR.
2. Perform the user-owned actual-size Blueprint/PIE acceptance at 1920x1080 and 1280x720.
3. Resolve review or visual acceptance findings, then decide whether the current changes are ready for the user to commit.
4. Start C09A only after the production runtime/HUD slice is accepted.
