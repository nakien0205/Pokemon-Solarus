# Public Battle UI Contracts

This folder contains the native public contracts used by Battle presentation, input, and HUD composition.

Use it to identify the presentation contract before changing its implementation under `Private/UI/`.

## Routing

### Display-ready HUD state

* `BattleHUDDisplayState.h`

Defines the display-ready state passed into the Battle HUD, including health and top-level command presentation.

Start here when the shape of data delivered to the HUD must change.

### Battle state → presentation adaptation

* `BattlePresentationAdapter.h`

Stateless boundary for converting observer-safe Battle snapshots into display-ready UI state.

Use this when changing what authoritative Battle information is projected for presentation.

### Top-level command UI

* `BattleCommandWidget.h`

Public contract for local Fight / Bag / Pokémon / Run focus, navigation, availability, and command-request signals.

The widget does not own Battle-core state.

### Root HUD

* `BattleHUDWidget.h`

Public HUD facade and lifecycle contract.

Use it for complete HUD-state application, child presentation coordination, command-menu forwarding, and HUD readiness.

### Player controller and input

* `BattlePlayerController.h`

Owns local Battle input/HUD integration.

Start here for Enhanced Input routing, HUD ownership, or local controller-to-widget interaction.

### GameMode composition

* `BattleGameMode.h`

Public composition/orchestration contract for creating and advancing the local Battle runtime and connecting it to presentation.

Battle mechanics remain owned by Battle-core contracts.

### Pokémon health panel

* `BattlePokemonHealthPanel.h`

Public visual health-panel contract.

Use it for display and visual HP animation behavior, not authoritative HP calculation.

## Presentation flow

```text
Battle runtime snapshot / decision
        ↓
BattlePresentationAdapter
        ↓
BattleHUDDisplayState
        ↓
BattleHUDWidget / BattleCommandWidget / BattlePokemonHealthPanel
        ↓
UMG presentation
```

`BattlePlayerController` supplies local input/HUD ownership, while `BattleGameMode` composes the Battle runtime around this presentation flow.

## Related directories

* `../Battle/` — authoritative Battle public contracts.
* `../../Private/UI/` — implementations of these UI contracts.
* `../../Private/Tests/` — functional presentation/UI tests.
* `Game/Content/UI/Battle/` — authored UMG/content assets.

## Do not read by default

Do not inspect every `UPROPERTY`, every widget implementation, or Battle UMG asset merely to understand the public presentation boundary.

Choose the contract above that matches the task, then follow it into `Private/UI/` only when implementation details are needed.

---

# `plan/battle_mechanics/README.md`

# Battle Mechanics Plan Routing

This directory contains the detailed Battle-system roadmap packages.

Do **not** read the packages sequentially and do not load the entire Battle plan set. Identify the package that owns the requested behavior and read only that package plus direct dependencies when necessary.

## Package routing

| Package | Owns                                          |
| ------- | --------------------------------------------- |
| `01-*`  | Live baseline and rules snapshot              |
| `02-*`  | Core contracts, events, and RNG               |
| `03-*`  | Stats, types, moves, and data adapters        |
| `04-*`  | Battle state, snapshots, and decisions        |
| `05-*`  | Actions, ordering, and targeting              |
| `06-*`  | Hit, damage, effects, and outcomes            |
| `07-*`  | Parties, switching, and replacements          |
| `08-*`  | Status, volatiles, field, and side conditions |
| `09-*`  | Abilities, held items, and Battle items       |
| `10-*`  | Encounters, capture, escape, and partner flow |
| `11-*`  | Canonical proof content                       |
| `12-*`  | Integration and release gate                  |

## Which index should I use?

```text
README.md
    = quick package routing

00-roadmap-index.md
    = overall roadmap and status

01–12 package files
    = detailed requirements / implementation packages

reference/modern-rules-snapshot.md
    = frozen modern Pokémon rules reference when exact rule lookup is needed
```

Use `00-roadmap-index.md` when roadmap status or package sequencing is the question.

Use the specific `01–12` package when implementing or validating behavior owned by that package.

Use `reference/modern-rules-snapshot.md` only when the task needs the exact frozen rules reference; do not load it as routine context.

## Agent routing rule

> Identify the owning package first. Read that package, then only the direct prerequisite or reference material required to resolve the task.

Do not treat this directory as one large specification that must be loaded before Battle work.
