<!-- STATUS -->

Mode: PLAYABLE
Epic: Battle System
Roadmap: Fast reusable Battle roadmap through strategic opponent AI
Milestone: Step 1 — Four-Move Battle Selection
State: Design accepted; implementation not started

<!-- /STATUS -->

# Active Project State

**Updated:** 2026-09-06

## Current Goal

Make **Four-Move Battle Selection** playable on top of the existing accepted
one-Pokemon battle.

The current milestone is Step 1 only.

Long-term Solarus scope and later Battle roadmap steps remain valid, but they do
not create current implementation work.

## Current Authorities

Primary current design:

* `design/gdd/four-move-battle-selection.md`

Roadmap:

* `design/gdd/systems-index.md`

Functional presentation/input authorities when relevant:

* `design/ux/battle-hud.md`
* `docs/engine-reference/unreal/modules/input.md`

Repository routing:

* `index.md`

Do not load older Battle plans, unrelated ADRs, or historical
reports unless the current task specifically depends on them.

## Implemented Baseline

The reusable one-Pokemon playable battle was accepted on 2026-09-02.

It already proves:

* Charizard versus Venusaur in FoundationMap;
* authoritative BattleEngine turn resolution;
* one legal player Fight action;
* automatic opponent action;
* PP, targeting, accuracy, damage, HP, fainting, and outcome;
* HUD refresh;
* victory/defeat;
* clean PIE restart.

Treat the existing BattleEngine and completed Battle mechanics as a stable
foundation.

Do not redesign or improve that foundation unless Step 1 proves a specific gap.

## Current Step 1 Scope

The accepted Four-Move Battle Selection design currently requires:

* a four-slot move-selector capacity;
* the approved temporary assignments:

  * Charizard: Swift, Earthquake, blank, blank;
  * Venusaur: Vine Whip, Earthquake, blank, blank;
* authored move order and blank-slot behavior;
* player selection from legal current BattleEngine decisions;
* unavailable move information;
* PP and effectiveness presentation;
* Struggle fallback;
* Move Details behavior;
* Battle Info functional behavior;
* the temporary uniform opponent policy;
* real BattleEngine turn resolution after both decisions;
* the presentation data required by this milestone.

The temporary loadout is a selector proof, not the final strategic move set.

## Required Architecture Work

The accepted Step 1 GDD explicitly requires two architecture decisions before
the affected implementation:

1. **Opponent-policy ADR**

   * Only define the reusable boundary required for the current temporary
     opponent policy.
   * Preserve current BattleEngine ownership and deterministic/replay
     guarantees required by the accepted GDD.
   * Do not design strategic AI or later multi-request systems.

2. **Presentation-composition ADR**

   * Only define the storage/composition/projection boundary required for Step 1
     move presentation and observer-safe selector information.
   * Do not design a general future presentation framework.

These ADRs are required because the accepted GDD currently requires them, not
because all PLAYABLE work normally requires architecture review.

Use the lean review rule from `AGENTS.md`:

**one review -> correction if required -> one confirmation**

Do not continue architecture-review loops for advisory future concerns.

## User-Owned Decision Still Needed

Before Confirm-hold behavior is implemented, the user must provide the positive
`T_hold` threshold required by the accepted GDD.

The key mapping itself is already closed:

* Arrow keys — navigation;
* `C` — Confirm;
* `X` — Cancel;
* `V` — Battle Info.

Do not reopen those bindings.

## Explicitly Out of Scope

Do not begin these while Step 1 is active:

* Command Child Flows / roadmap Step 2;
* First Battle Item / Step 3;
* Fifty-Pokemon Switching Set / Step 4;
* broader Move Pools / Step 5;
* Special-Effect Expansion / Step 6;
* Strategic Opponent AI / Step 7;
* a general learnset or TM database;
* additional unsupported move-effect behavior;
* C11A deferred catalog-gap closure;
* Cry for Help or wild reinforcement;
* unrelated BattleEngine refactoring;
* replay or transaction improvement not required by Step 1;
* unrelated test expansion;
* visual redesign or user-owned art changes.

Future requirements may be noted as technical debt or future work. They must not
expand Step 1.

## Existing Deferred State

C11A remains:

`INCOMPLETE_CATALOG_DEFERRED`

Its six catalog gaps remain deferred until their required accepted data arrives.

C11B is accepted complete.

Neither state is part of the current Step 1 implementation unless new accepted
catalog data directly triggers one of the existing deferred guards.

Do not rerun C11 release-gate work merely because Step 1 is active.

## Validation Policy

Use validation appropriate to Step 1.

During implementation:

* run focused selector/policy/presentation tests for the responsibility changed;
* run a normal `PokemonSolarusEditor Win64 Development` build when C++ changes;
* rerun older Battle filters only when shared Battle code changed in a way that
  can affect them;
* compile affected Blueprint/widget assets when relevant;
* verify the player-facing flow in actual PIE.

Do not run the complete C11 production evidence process.

At Step 1 completion, perform the accepted GDD's milestone validation and one
broader regression/review appropriate to the shared code actually changed.

## Current Development Rule

For Step 1:

> Build only what is necessary to make Four-Move Battle Selection playable.

The full game vision remains valid.

The later Battle roadmap remains valid.

Neither is permission to implement later systems now.

## Next

1. Complete the bounded opponent-policy ADR required by the accepted Step 1 GDD.
2. Complete the bounded presentation-composition ADR required by the accepted
   Step 1 GDD.
3. Obtain the user's `T_hold` value before implementing hold behavior.
4. Implement Step 1 in PLAYABLE mode.
5. Run focused validation and a complete Step 1 PIE battle.
6. Stop and obtain Step 1 acceptance before beginning roadmap Step 2.

Historical Battle implementation/evidence remains available in the repository
and Git history. It is not active-session context unless a current task needs it.
