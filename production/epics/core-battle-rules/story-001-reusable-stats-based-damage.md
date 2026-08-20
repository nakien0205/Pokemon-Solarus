# Story 001: Reusable Stats-Based Damage

> **Epic**: Core Battle Rules (lightweight story governed by an approved quick spec)
> **Status**: Complete
> **Layer**: Core
> **Type**: Logic
> **Estimate**: 4 hours
> **Manifest Version**: N/A — control manifest not yet created
> **Last Updated**: 2026-08-20

## Context

**GDD**: `design/gdd/game-concept.md` — `Global Base Damage Rule`

**Approved Quick Spec**:
`design/quick-specs/reusable-stats-based-damage-addition-2026-08-20.md`

**Requirement Tracking**: N/A — `docs/architecture/tr-registry.yaml`
contains no registered requirements. This story directly quotes the approved
GDD rule and embeds the complete behavior below.

**GDD Requirement**:

> All damaging moves use the same base-damage calculation. The calculation
> accepts the attacker's level, the move's power and damage category, and both
> Pokemon's calculated battle stats. It must not depend on a specific Pokemon
> or move identity.

**ADR Governing Implementation**: No ADR applies — this story adds no new
architectural pattern and preserves the approved plain-C++ battle-rules seam.

**Engine**: Unreal Engine 5.8.1 | **Risk**: LOW

**Engine Notes**: The calculation uses plain C++ value types, `int32`, `int64`,
and the existing Unreal Automation Testing Framework. No new or post-cutoff
engine API is introduced. Validate with the pinned Unreal Engine 5.8.1 build.

**Control Manifest Rules**: N/A — manifest not yet created. Apply the existing
project constraints instead:

- Keep battle rules independent of a World, Actor, Blueprint, or UI object.
- Keep Charizard-first deterministic action order for this milestone.
- Do not introduce GAS, replication, multiplayer, Data Assets, or Data Tables.

## Behavior Contract

Add one reusable base-damage calculator with this plain-C++ contract:

```cpp
static bool TryCalculateDamage(
    int32 AttackerLevel,
    const FPokemonBattleStats& AttackerStats,
    const FPokemonBattleStats& DefenderStats,
    EBattleMoveCategory MoveCategory,
    int32 MovePower,
    int32& OutDamage);
```

`EBattleMoveCategory` is declared with the calculator and contains `Physical`,
`Special`, and `Status`. The calculator receives no actor, species, or move
identity.

- Physical selects the attacker's Attack and the defender's Defense.
- Special selects the attacker's Special Attack and the defender's Special
  Defense.
- Status does not calculate damage.
- Level must be from 1 through 100.
- Move power and the selected offensive and defensive stats must be greater
  than zero.
- Invalid input returns `false`, sets `OutDamage` to zero, and never divides by
  zero.
- Intermediate arithmetic uses `int64`.
- Successful damage is at least one.
- `ApplyDamage()` remains responsible for clamping current HP at zero.

The deterministic calculation is:

```text
Level Factor = floor((2 * Attacker Level) / 5) + 2
Scaled Damage = floor(Level Factor * Move Power * Offensive Stat / Defensive Stat)
Base Damage = floor(Scaled Damage / 50) + 2
```

Each shown division rounds down before the next step. STAB, type effectiveness,
random rolls, critical hits, burn, and all other final-damage modifiers remain
outside this story.

## Current Battle Fixture

Both participants use level 50, neutral nature, 31 IVs, and 0 EVs. Runtime stat
generation is not part of this story; these calculated values are supplied to
the existing battle state.

| Stat | Charizard | Venusaur |
|---|---:|---:|
| Max HP | 153 | 155 |
| Attack | 104 | 102 |
| Defense | 98 | 103 |
| Special Attack | 129 | 120 |
| Special Defense | 105 | 120 |
| Speed | 120 | 100 |

The resolver maps only the current two moves to reusable calculator inputs:

| Move | Category | Power |
|---|---|---:|
| Flamethrower | Special | 90 |
| Vine Whip | Physical | 45 |

The move-to-category and move-to-power mapping may remain in the current
resolver. Species and move branches are forbidden only inside the generic
calculator.

## Acceptance Criteria

- [ ] **AC-1 — Generic calculator:** The calculator accepts any supplied level,
  calculated attacker stats, calculated defender stats, move category, and
  power. Physical uses Attack and Defense; Special uses Special Attack and
  Special Defense. Its signature and implementation contain no `EBattleActor`,
  `EBattleMove`, Charizard, Venusaur, Flamethrower, or Vine Whip dependency.
- [ ] **AC-2 — Safe validation:** Levels below 1 or above 100, non-positive move
  power, Status category, and non-positive selected offensive or defensive
  stats return `false`, set output damage to zero, and do not divide by zero.
  Valid calculation intermediates use `int64`.
- [ ] **AC-3 — Deterministic results:** The approved fixtures calculate 44 base
  damage for Flamethrower and 22 for Vine Whip. At least one arbitrary
  non-Charizard and non-Venusaur stats fixture passes through the same API and
  matches the documented integer-rounding formula.
- [ ] **AC-4 — Battle state values:** The current battle stores and exposes
  level 50 for each participant, starts Charizard at 153 of 153 HP with stats
  104/98/129/105/120, and starts Venusaur at 155 of 155 HP with stats
  102/103/120/120/100. The state consumes calculated stats and does not generate
  base-stat, IV, EV, or nature values.
- [ ] **AC-5 — Resolver integration:** The resolver supplies category and power
  for the selected move, calls the generic calculator, and applies damage only
  after a successful calculation. The first resolved turn applies 44 to
  Venusaur and 22 to Charizard, leaving Venusaur at 111 HP and Charizard at 131
  HP. Fixed damage constants 80 and 50 and `GetDamage()` are removed.
- [ ] **AC-6 — Complete battle regression:** The deterministic sequence ends
  after four accepted Attack requests at Charizard 87 HP, Venusaur 0 HP, and
  `CharizardVictory`. The fourth turn contains only Charizard's action, applies
  the target's remaining 23 HP, skips Venusaur after fainting, and preserves
  post-victory request rejection.
- [ ] **AC-7 — Presentation regression:** Presenter and runtime views display
  initial HP as 153 of 153 and 155 of 155 and propagate the new damage sequence.
  The widget no longer initializes either participant to hard-coded 200 of 200;
  it continues receiving HP through battle view state. No UI layout or styling
  changes are made.
- [ ] **AC-8 — Verification:** `PokemonSolarusEditor` Win64 Development builds
  successfully. Focused calculator tests and the complete
  `PokemonSolarus.Battle` automation suite pass without warnings. Test or Editor
  execution leaves no unrelated configuration change.

## Implementation Notes

1. Create `EBattleMoveCategory` and `FBattleDamageCalculator` in the new
   calculator header and implement only the generic validation, stat selection,
   and integer formula in its source file.
2. Store participant level separately from `FPokemonBattleStats`, expose it
   through `FBattlePokemonState`, and supply it when constructing a battle from
   calculated stats. Do not turn the six-stat struct into a species database.
3. Keep `FBattleState::CreatePlaceholder()` as the current coordinator entry
   point, but replace its historical HP-only fixture with the approved level-50
   calculated stats.
4. Keep the existing Charizard-first action order, fainting check, victory
   result, and post-victory rejection behavior unchanged.
5. Map Flamethrower to Special power 90 and Vine Whip to Physical power 45 in
   the resolver. Do not add move data assets or a general move database.
6. Invoke `ApplyDamage()` only when `TryCalculateDamage()` succeeds. A failed
   calculation leaves target HP unchanged and reports zero applied damage.
7. Remove only the widget's hard-coded 200-of-200 initialization. Do not alter
   its hierarchy, dimensions, colors, input, labels, or command layout.
8. Update every stale 200/80/50 and three-turn expectation in the scoped battle
   tests. Preserve unrelated runtime-stage and input assertions.

## Exact Changeset

Create:

- `Game/Source/PokemonSolarus/Public/Battle/BattleDamageCalculator.h`
- `Game/Source/PokemonSolarus/Private/Battle/BattleDamageCalculator.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleDamageCalculatorTests.cpp`

Modify:

- `Game/Source/PokemonSolarus/Public/Battle/BattleState.h`
- `Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp`
- `Game/Source/PokemonSolarus/Public/Battle/BattleTurnResolver.h`
- `Game/Source/PokemonSolarus/Private/Battle/BattleTurnResolver.cpp`
- `Game/Source/PokemonSolarus/Private/Presentation/PlaceholderBattleWidget.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleLogicTests.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleCoordinatorTests.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/PlaceholderBattlePresenterTests.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/PlaceholderBattleRuntimeTests.cpp`

No other production, test, configuration, map, or asset file is in scope.

## Out of Scope

- A database containing every Pokemon or move.
- Runtime base-stat, IV, EV, or nature calculation.
- Types, STAB, effectiveness, random rolls, critical hits, accuracy, or burn.
- Stat stages, Speed-based ordering, PP, and secondary effects.
- Data Assets, Data Tables, GAS, replication, or multiplayer.
- Maps, Pokemon assets, cameras, scene placement, UI layout, or styling.
- Renaming the existing placeholder classes or expanding the command menu.
- Git initialization or commits.

## Performance

No measurable performance impact is expected. Each action adds a fixed number
of integer operations with no allocation, file access, World lookup, or Actor
dependency. No broader performance budget exists for this plain-C++ logic.

## QA Test Cases

### AC-1 — Generic calculator and category mapping

- **Given** attacker and defender stats whose physical and special values are
  deliberately different.
- **When** the calculator runs once as Physical and once as Special.
- **Then** each result matches the formula using only its category's offensive
  and defensive pair.
- **Edge cases**: Use an arbitrary stats fixture unrelated to the current two
  participants; inspect the calculator source for forbidden actor, move, or
  species dependencies.

### AC-2 — Invalid input safety

- **Given** output damage initialized to a non-zero sentinel.
- **When** the calculator receives level 0, level 101, zero power, negative
  power, Status, zero selected offense, or zero selected defense.
- **Then** every call returns `false` and resets output damage to zero without a
  crash or divide-by-zero error.
- **Edge cases**: Confirm levels 1 and 100 are accepted when all other inputs
  are valid.

### AC-3 — Known and arbitrary deterministic results

- **Given** the approved level-50 Charizard and Venusaur calculated stats.
- **When** Flamethrower is evaluated as Special power 90 and Vine Whip as
  Physical power 45.
- **Then** results are exactly 44 and 22.
- **Edge cases**: Verify an arbitrary level-50, power-100, Attack-200 versus
  Defense-100 Physical fixture returns 90 using the same calculator.

### AC-4 — Level and calculated stats in battle state

- **Given** a newly created current battle.
- **When** both participant states are read before any action.
- **Then** levels, all six stats, maximum HP, and current HP exactly match the
  approved table.
- **Edge cases**: Existing non-negative stat clamping and HP-at-maximum behavior
  remain covered.

### AC-5 — Resolver integration

- **Given** the current battle before its first turn.
- **When** one turn resolves.
- **Then** Charizard acts first for 44 applied damage, Venusaur acts second for
  22, and HP becomes Charizard 131 and Venusaur 111.
- **Edge cases**: A deliberately invalid calculated-damage input applies zero
  damage and leaves target HP unchanged; the resolver source contains neither
  fixed damage constants nor `GetDamage()`.

### AC-6 — Four-turn victory and terminal state

- **Given** a fresh current battle.
- **When** four Attack requests resolve in order.
- **Then** turns one through three contain both actions; turn four contains only
  Charizard's 23 applied damage; the final state is Charizard 87, Venusaur 0,
  and `CharizardVictory`.
- **Edge cases**: A fifth request is rejected, exposes no actions, and leaves HP
  and result unchanged.

### AC-7 — Presenter, runtime, and widget HP propagation

- **Given** the presenter and runtime widget bound to a fresh battle view state.
- **When** initial state and the four-turn sequence are presented.
- **Then** initial text and numeric HP are 153 of 153 and 155 of 155, HP bars
  start full, each action propagates its calculated damage, and the final faint
  and victory presentation remains intact.
- **Edge cases**: Source inspection confirms the widget contains no hard-coded
  200-of-200 initialization and unrelated layout assertions remain unchanged.

### AC-8 — Build and complete regression suite

- **Given** the scoped implementation and tests in the pinned UE 5.8.1 project.
- **When** the Win64 Development Editor target is built and focused calculator
  tests plus `PokemonSolarus.Battle` are executed.
- **Then** the build succeeds and all tests pass without warnings.
- **Edge cases**: Inspect project configuration after execution and remove no
  pre-existing user setting; fail the story if the run introduces an unrelated
  configuration change.

## Test Evidence

**Story Type**: Logic

**Required evidence**:

- `Game/Source/PokemonSolarus/Private/Tests/BattleDamageCalculatorTests.cpp`
  exists and its focused tests pass.
- Updated regression assertions pass in
  `Game/Source/PokemonSolarus/Private/Tests/BattleLogicTests.cpp`,
  `BattleCoordinatorTests.cpp`, `PlaceholderBattlePresenterTests.cpp`, and
  `PlaceholderBattleRuntimeTests.cpp`.
- The complete `PokemonSolarus.Battle` suite passes without warnings.
- The `PokemonSolarusEditor` Win64 Development build succeeds.
- A post-run configuration check shows no unrelated change.

**Status**: Verified — all required evidence passed on 2026-08-20.

## Dependencies

- **Depends on**: None — calculated numeric stats and the plain-C++ battle seam
  already exist in the live project.
- **Unlocks**: Later separately approved global damage modifiers and reusable
  Pokemon or move data work; neither is part of this story.

## Completion Notes

**Completed**: 2026-08-20

**Criteria**: 8/8 passing

**Deviations**: None. The story used the explicitly approved one-story
exception for the currently missing TR, ADR, and control-manifest artifacts.

**Test Evidence**: Logic —
`Game/Source/PokemonSolarus/Private/Tests/BattleDamageCalculatorTests.cpp`;
focused calculator tests passed 4/4 and the complete `PokemonSolarus.Battle`
suite passed 33/33 with zero warnings or failures. The Win64 Development Editor
build succeeded, and post-test configuration hashes matched the pre-run
baseline.

**Code Review**: Complete — APPROVED WITH SUGGESTIONS; no required changes.

**Review Notes**: Optional future hardening may add an exact level-sensitivity
result, direct overflow-rejection coverage, and repeated real-widget view-state
updates. The redundancy audit found 33 unique automation paths and no unused
Story 001 test helper, so no source test was removed.
