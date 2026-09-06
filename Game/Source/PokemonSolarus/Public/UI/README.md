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
