# Battle UI Implementation

This folder contains the native implementation of the Battle presentation layer and its runtime composition/input wiring.

The presentation boundary is:

```text
BattleEngine owns mechanics and authoritative state
        ↓
observer-safe Battle snapshot / decision request
        ↓
BattlePresentationAdapter
        ↓
display-ready UI state
        ↓
HUD / command / health widgets
```

UI code must not become a second Battle-rules engine.

## Agent routing rule

Start with the implementation file that owns the presentation responsibility.

Do not read all UI files for every Battle UI issue, and do not move Battle legality, damage, targeting, availability, or other mechanics into UI code.

## Responsibility groups

### Battle state → display state

Start with:

* `BattlePresentationAdapter.cpp`

Use it when converting observer-safe Battle facts into display-ready command or HUD state, including typed unavailable reasons and display validation.

The adapter consumes Battle facts. It should not independently calculate Battle mechanics.

Public contract:

* `../../Public/UI/BattlePresentationAdapter.h`
* `../../Public/UI/BattleHUDDisplayState.h`

### Runtime composition and Battle advancement

Start with:

* `BattleGameMode.cpp`

This is the Battle runtime composition/orchestration boundary. It initializes the runtime source and Battle engine, connects the local player/controller, refreshes presentation, and coordinates Battle progression.

Use it for runtime wiring or the handoff between accepted UI requests and Battle operations.

Battle-rule authority remains in `Private/Battle/` / `Public/Battle/`, not in the GameMode.

### Local input and HUD ownership

Start with:

* `BattlePlayerController.cpp`

Use it for:

* Enhanced Input wiring;
* Battle navigation/Confirm/Cancel routing;
* HUD creation and viewport ownership;
* local presentation readiness;
* forwarding input to the active HUD.

Input routing may select or request an action, but legality remains Battle-owned.

### Top-level command menu

Start with:

* `BattleCommandWidget.cpp`

This owns local Fight / Bag / Pokémon / Run menu behavior such as focus, cardinal navigation, Confirm handling, and emitted local command requests.

It intentionally does not read or mutate Battle-core state.

### Root Battle HUD

Start with:

* `BattleHUDWidget.cpp`

Use it for:

* root HUD lifecycle;
* structural child binding;
* applying complete display state;
* command-widget facade behavior;
* health-panel coordination;
* presentation/input gating.

Do not place Battle mechanics here.

### Pokémon health presentation

Start with:

* `BattlePokemonHealthPanel.cpp`

Use it for health-panel display behavior and visual HP interpolation.

The panel presents authoritative HP values; visual animation must not mutate or redefine Battle HP.

## Related directories

* `Game/Source/PokemonSolarus/Public/UI/` — public contracts for these implementations.
* `Game/Source/PokemonSolarus/Public/Battle/` — authoritative Battle state/decision contracts consumed by presentation.
* `Game/Source/PokemonSolarus/Private/Battle/` — Battle mechanics/runtime implementation.
* `Game/Source/PokemonSolarus/Private/Tests/` — command UI, presentation-adapter, HUD lifecycle, and runtime-presentation tests.
* `Game/Content/UI/Battle/` — authored Battle UMG/content assets.

Current native code references assets such as:

* `WBP_BattleHUD.uasset`
* `WBP_BattleCommandUI.uasset`
* `WBP_BattlePokemonHealthPanel.uasset`

Inspect those assets only when the task actually concerns structural Blueprint bindings or user-requested presentation work.

## Presentation boundary

A useful ownership test is:

```text
Does this value answer "what is true in the Battle?"
    → BattleEngine / Battle contracts own it.

Does this value answer "how should an already-authoritative fact be exposed to the UI?"
    → presentation adapter / UI may own it.

Does this concern local focus, widget lifecycle, animation, or input forwarding?
    → UI may own it.
```

Do not infer mechanics from what happens to be displayed.

## Do not read by default

Do not automatically inspect:

* every file in `Private/Battle/`;
* Widget Blueprint visuals or art assets;
* UI reference art;
* unrelated input assets;
* all presentation tests.

For functional UI work, identify the native presentation seam first and inspect Blueprint/content assets only when the native contract requires it.
