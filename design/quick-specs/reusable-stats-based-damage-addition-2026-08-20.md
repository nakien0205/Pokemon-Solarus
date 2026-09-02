# Quick Design Spec: Reusable Stats-Based Damage

**Type:** Addition  
**System:** Core battle rules  
**GDD Reference:** design/gdd/game-concept.md — MVP Definition and Formulas  
**Date:** 2026-08-20  
**Status:** Approved

## Change Summary

Make BattleEngine the sole damage authority through one generic, deterministic
damage calculator.

"Reusable for all Pokemon" means the calculator accepts any Pokemon's level
and calculated stats. It contains no Charizard, Venusaur, or species-specific
branches. Only the prototype data remains species-specific.

This addition does not create or import a database containing every Pokemon.

## Motivation

The current resolver ignores Attack, Defense, Special Attack, and Special
Defense. This makes defensive stats meaningless.

The new calculation establishes the reusable seam needed by every damaging
move while remaining small enough to validate with focused tests.

## Design Delta

The approved Game Concept requires BattleEngine to own current HP and damage
using the authoritative scenario, catalog, calculated stats, deterministic RNG,
and transactional resolution. No presentation or runtime-orchestration layer
supplies a separate result.

## New Rules and Values

### Reusable Calculator Contract

The proposed plain-C++ interface is:

    static bool TryCalculateDamage(
        int32 AttackerLevel,
        const FPokemonBattleStats& AttackerStats,
        const FPokemonBattleStats& DefenderStats,
        EBattleMoveCategory MoveCategory,
        int32 MovePower,
        int32& OutDamage);

Rules:

- The calculator must not receive EBattleActor, Pokemon species, or move
  identity.
- Physical moves use Attack and Defense.
- Special moves use Special Attack and Special Defense.
- Status moves do not enter the damage calculation.
- Level must be between 1 and 100.
- Power and the selected offensive and defensive stats must be greater than
  zero.
- Invalid input returns false, sets OutDamage to zero, and never divides by
  zero.
- Intermediate arithmetic uses int64.
- Successful damage is at least one.
- Current-HP clamping remains the responsibility of ApplyDamage().

### Deterministic Formula

    LevelFactor = floor((2 * Level) / 5) + 2

    ScaledDamage =
        floor(
            LevelFactor * MovePower * OffensiveStat
            / DefensiveStat
        )

    BaseDamage = floor(ScaledDamage / 50) + 2

This is the reusable base-damage stage used by the Pokemon Showdown calculator.
Random rolls and later modifiers remain separate stages.

Reference:
https://github.com/smogon/damage-calc/blob/master/calc/src/mechanics/util.ts#L448-L455

### Prototype Pokemon Values

The prototype uses level 50, neutral nature, 31 IVs, and 0 EVs.

| Stat | Charizard | Venusaur |
|---|---:|---:|
| Max HP | 153 | 155 |
| Attack | 104 | 102 |
| Defense | 98 | 103 |
| Special Attack | 129 | 120 |
| Special Defense | 105 | 120 |
| Speed | 120 | 100 |

These are frozen prototype inputs derived from current base stats and the
modern stat formula. The stat-generation formula is not implemented in this
addition. A later reusable stat generator can produce the same
FPokemonBattleStats values.

References:

- https://github.com/smogon/damage-calc/blob/master/calc/src/stats.ts#L151-L179
- https://github.com/smogon/pokemon-showdown/blob/master/data/pokedex.ts#L29-L35
- https://github.com/smogon/pokemon-showdown/blob/master/data/pokedex.ts#L105-L111

### Move Inputs

| Move | Category | Power | Stats Used |
|---|---|---:|---|
| Flamethrower | Special | 90 | Charizard Special Attack / Venusaur Special Defense |
| Vine Whip | Physical | 45 | Venusaur Attack / Charizard Defense |

References:

- https://github.com/smogon/pokemon-showdown/blob/master/data/moves.ts#L5599-L5604
- https://github.com/smogon/pokemon-showdown/blob/master/data/moves.ts#L20351-L20356

Calculated damage:

- Flamethrower: 44
- Vine Whip: 22

### Expected Battle Sequence

| Turn | Flamethrower applied | Vine Whip applied | Charizard HP | Venusaur HP |
|---:|---:|---:|---:|---:|
| Start | — | — | 153 | 155 |
| 1 | 44 | 22 | 131 | 111 |
| 2 | 44 | 22 | 109 | 67 |
| 3 | 44 | 22 | 87 | 23 |
| 4 | 23 | 0 | 87 | 0 |

Charizard still acts first and wins. Venusaur is skipped after fainting. The
battle now lasts four turns rather than three.

## Affected Systems

| System | Impact | Action Required |
|---|---|---|
| Battle stats/state | Store and expose combatant level alongside calculated stats | Update state constructors and current prototype fixture |
| Move resolution | Use category, power, and calculator input owned by BattleEngine | Update resolver |
| Coordinator | Consumes the revised current prototype through the existing state factory | Update stale comments only if needed |
| Presenter/runtime | Receives new HP and damage values through existing resolution data | No production presenter logic change expected |
| Battle widget | Bind authoritative HP from battle view state | Continue binding HP from battle view state |
| Automated tests | Calculator behavior needs direct and full-battle proof | Add focused calculator tests and update full battle regression expectations |
| Game concept GDD | BattleEngine owns the current damage contract | Add a Global Base Damage Rule section after separate approval |

## Exact Changeset

Create:

- Game/Source/PokemonSolarus/Public/Battle/BattleDamageCalculator.h
- Game/Source/PokemonSolarus/Private/Battle/BattleDamageCalculator.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleDamageCalculatorTests.cpp

Modify:

- Game/Source/PokemonSolarus/Public/Battle/BattleState.h
- Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp
- Game/Source/PokemonSolarus/Public/Battle/BattleTurnResolver.h
- Game/Source/PokemonSolarus/Private/Battle/BattleTurnResolver.cpp
- Game/Source/PokemonSolarus/Private/Presentation/PlaceholderBattleWidget.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleLogicTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/BattleCoordinatorTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/PlaceholderBattlePresenterTests.cpp
- Game/Source/PokemonSolarus/Private/Tests/PlaceholderBattleRuntimeTests.cpp

The widget continues reading authoritative HP from battle view state; no layout
changes are included.

## Acceptance Criteria

- [ ] The damage calculator has no Pokemon- or move-specific branches.
- [ ] An arbitrary test Pokemon can use the same calculator.
- [ ] Physical moves use Attack and Defense.
- [ ] Special moves use Special Attack and Special Defense.
- [ ] Invalid inputs fail safely without damaging HP.
- [ ] Flamethrower calculates 44 damage.
- [ ] Vine Whip calculates 22 damage.
- [ ] The prototype starts at Charizard 153 HP and Venusaur 155 HP.
- [ ] The deterministic four-turn sequence ends at Charizard 87 and Venusaur 0.
- [ ] Charizard-first ordering, fainting, victory, and post-victory rejection
      remain intact.
- [ ] BattleEngine is the sole producer of the applied damage result.
- [ ] The PokemonSolarusEditor Win64 Development build succeeds.
- [ ] Focused damage tests and the complete PokemonSolarus.Battle suite pass
      without warnings.
- [ ] No unrelated configuration changes remain.

## Explicitly Excluded

- A database containing every Pokemon.
- Runtime base-stat, IV, EV, and nature calculation.
- Types, STAB, effectiveness, randomness, critical hits, accuracy, and burn.
- -6 to +6 stat stages.
- Speed-based action ordering.
- PP and secondary move effects.
- Data Assets or Data Tables.
- Maps, Pokemon assets, cameras, scene placement, or UI layout.
- Commits.

## GDD Update Required?

Yes — applied to design/gdd/game-concept.md.

The completed initial-placeholder section must remain as historical milestone
evidence. The new "Global Base Damage Rule" section records only reusable rules
and deferred modifiers. Match-specific values and outcomes remain in this quick
spec as implementation fixtures and test evidence; they are not global design
rules.

The battle-system interview handoff already establishes modern official rules
as the eventual reusable baseline, so no handoff rewrite is required for this
bounded intermediate slice.
