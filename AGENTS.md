# Pokemon Solarus — Codex Repository Instructions

## Project

- The authoritative Unreal project is `Game/PokemonSolarus.uproject`.
- Engine: Unreal Engine 5.8.1. Primary implementation language: C++.
- Read `production/session-state/active.md` before continuing ongoing work.
- Live source, the current roadmap package, the worktree, and exported Unreal
  Automation reports override historical status notes.
- Read `docs/registry/external-assets.yaml` before importing or using third-party
  assets. Source files represented by `deferred` entries are provenance
  references only and are not approved for import, use, repository inclusion,
  or distribution.

## Collaboration and ownership

- Meaningful edits follow: Question -> Options -> Decision -> Draft -> Approval.
- Preserve unrelated dirty work. Do not rewrite or delete user changes.
- Do not commit, push, stage, create branches, or alter Git history unless the
  user explicitly asks.
- The user owns frontend/UI/UX visual design and visual assets: layout, styling,
  art, materials, textures, composition, and motion appearance.
- Codex owns code-behind, mechanics, state, input routing, data contracts,
  adapters, validation, and automated tests. Do not alter visual assets without
  a task-specific exception.

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
