# Quick Design Spec: C11A Catalog-Gap Closure

**Type:** Addition  
**System:** Canonical battle proof content and C11A integration coverage  
**GDD Reference:** `design/gdd/game-concept.md` — Game Pillars  
**Date:** 2026-09-02  
**Status:** Approved as a design package on 2026-09-02; implementation is not yet authorized

## Change Summary

Close the six remaining C11A catalog gaps by adding the smallest verified set
of proof content: Cryogonal, Rampardos, Dragapult, Waterfall, Breaking Swipe,
Infiltrator, and Icy Rock.

This package supplies real test subjects for existing Solarus battle rules. It
does not redesign those rules, add the full Pokedex, or reopen completed C11B
work.

## Motivation

C11A's existing tests are validated, but C11A remains incomplete because its
accepted catalog cannot exercise six required battle effects:

1. Snow's physical Defense increase for an Ice-type defender.
2. Sandstorm's Special Defense increase for a Rock-type defender.
3. Sun and Rain changing Water-type damage.
4. Misty Terrain reducing Dragon-type damage against a grounded target.
5. Bypassing Substitute and opposing side protections.
6. An approved condition lasting eight turns instead of five.

Adding arbitrary test-only subjects would not prove the real catalog works.
The package therefore adds a small set of official Pokemon Showdown content
that can exercise all six effects through ordinary battle behavior.

## Design Delta

The approved Game Concept says:

> Battles should feel recognizably like modern Pokemon while documenting every
> intentional Solarus exception.

It also says:

> Build the smallest reusable boundary needed by the current milestone,
> without implementing speculative future systems.

This package does not change either rule. It expands the canonical proof
catalog only enough to close the six named gaps using verified modern Pokemon
behavior.

## Source Authority

All Pokemon types, base stats, Ability choices, move details, learnsets, and
battle-effect details in this package come from the official Pokemon Showdown
website snapshot fetched on 2026-09-02:

- [Pokemon data](https://play.pokemonshowdown.com/data/pokedex.json)
- [Move data](https://play.pokemonshowdown.com/data/moves.json)
- [Ability data](https://play.pokemonshowdown.com/data/abilities.js)
- [Item data](https://play.pokemonshowdown.com/data/items.js)
- [Learnset data](https://play.pokemonshowdown.com/data/learnsets.json)

Pokemon Showdown does not publish catch rates. Under the user's explicit
2026-09-02 approval, the existing project-approved PokeAPI source may be used
for the three new `capture_rate` values only:

- Commit: `7af36d9f3424366ffc46e90d94c8bc120df39cd0`
- File: `data/v2/csv/pokemon_species.csv`
- File fingerprint:
  `9878F19C0637095CDD9A4134B4AAC8FB2B64776D3BDC599AA68F15C3A011B87C`

No other fact may be taken from PokeAPI for this package. If an official source
changes, the difference must be reviewed; it must not be silently accepted.

## Approved Content

### Pokemon

The six base-stat columns below are HP, Attack, Defense, Special Attack,
Special Defense, and Speed.

| Pokemon | Type | Base stats | Selected Ability | Catch rate | Purpose |
|---|---|---|---|---:|---|
| Cryogonal | Ice | 80 / 50 / 50 / 95 / 135 / 105 | Levitate | 25 | Proves Snow's Ice-type Defense increase and provides an airborne Misty Terrain comparison |
| Rampardos | Rock | 97 / 165 / 60 / 65 / 50 / 58 | Mold Breaker | 45 | Proves Sandstorm's Rock-type Special Defense increase and supplies a grounded Misty Terrain target |
| Dragapult | Dragon / Ghost | 88 / 120 / 75 / 100 / 75 / 142 | Infiltrator | 45 | Proves Dragon damage and Infiltrator behavior |

Cryogonal officially learns Snowscape. Dragapult officially learns Breaking
Swipe, Flamethrower, and Will-O-Wisp. The existing Gyarados officially learns
Waterfall.

Only the selected Ability shown above is required for each proof record. This
does not approve every Ability that each species can normally have.

### Moves

| Move | Official details | Official effect | Purpose |
|---|---|---|---|
| Waterfall | Water, physical, 80 power, 100% accuracy, 15 uses, contact, one selected target | 20% chance to make the target flinch | Proves Water damage in Sun and Rain using the existing Gyarados |
| Breaking Swipe | Dragon, physical, 60 power, 100% accuracy, 15 uses, contact, all adjacent opponents | Always lowers the Attack of every opponent it hits by one stage | Proves Misty Terrain's Dragon reduction and Mist bypass through Infiltrator |

Official reference pages:

- [Waterfall](https://dex.pokemonshowdown.com/moves/waterfall)
- [Breaking Swipe](https://dex.pokemonshowdown.com/moves/breakingswipe)

### Ability

**Infiltrator:** The user's moves ignore Substitute and the opposing side's
Reflect, Light Screen, Safeguard, Mist, and Aurora Veil.

This is an Ability belonging to Dragapult. The package must not invent a
special bypass property for Waterfall, Breaking Swipe, or any other move.

Official reference: [Infiltrator](https://dex.pokemonshowdown.com/abilities/infiltrator)

### Held Item

**Icy Rock:** When its holder uses Snowscape, Snow lasts eight turns instead
of five.

The package must not make every use of Snowscape last eight turns. Without an
active Icy Rock, Snow still lasts five turns.

Official reference: [Pokemon Showdown item data](https://play.pokemonshowdown.com/data/items.js)

## Existing Rules Exercised by the Package

- Snow causes no end-of-turn damage. While Snow is active, an Ice-type
  Pokemon's Defense is multiplied by 1.5 when taking a physical attack.
- Sandstorm multiplies a Rock-type Pokemon's Special Defense by 1.5 and does
  not damage Rock-type Pokemon at the end of the turn.
- Rain multiplies Water-type attack damage by 1.5.
- Sun multiplies Water-type attack damage by 0.5.
- Misty Terrain multiplies Dragon-type attack power by 0.5 against grounded
  Pokemon. Airborne Pokemon do not receive this protection.
- Magic Room temporarily turns off held-item effects. If Magic Room is already
  active when the holder uses Snowscape, Icy Rock does not extend Snow.

This package uses Snow, not Hail. Hail is outside the approved modern proof
rules, and the package must not add it as a substitute.

## Plain-Language Test Plan

### Test A — Official data

Confirm that every new Pokemon, move, Ability, and item matches the approved
source snapshot. Confirm that no unrelated catalog entry changes.

### Test B — Snow

Use the same physical attack against Cryogonal once without Snow and once
during Snow, with every other input kept the same. Confirm Snow gives
Cryogonal 50% more physical Defense. Also confirm Snow itself causes no
end-of-turn damage.

### Test C — Sandstorm

Use the same special attack against Rampardos once without Sandstorm and once
during Sandstorm, with every other input kept the same. Confirm Sandstorm gives
Rampardos 50% more Special Defense. Also confirm Rampardos takes no Sandstorm
damage at the end of the turn.

### Test D — Water damage in weather

Have the existing Gyarados use Waterfall under three otherwise identical
conditions:

1. Without weather, it deals normal Water damage.
2. During Sun, it deals half the normal Water damage.
3. During Rain, it deals 50% more Water damage.

### Test E — Waterfall

Confirm Waterfall has 80 power, 100% accuracy, 15 uses, makes contact, and
targets one opponent. Confirm its 20% flinch chance succeeds when the official
chance says it should and fails when it should not.

### Test F — Misty Terrain

Have Dragapult use Breaking Swipe against grounded Rampardos with and without
Misty Terrain, keeping every other input the same. Confirm Misty Terrain halves
the Dragon-type damage.

Repeat against Cryogonal. Because Levitate makes Cryogonal airborne, confirm
Misty Terrain does not reduce the Dragon-type damage against it.

### Test G — Breaking Swipe

In a Double Battle, confirm Breaking Swipe hits every adjacent opponent, never
hits Dragapult's ally, and always lowers the Attack of each opponent it hits by
one stage. Also confirm its official power, accuracy, uses, type, and contact
behavior.

### Test H — Infiltrator and damage protection

Confirm Dragapult's physical attack ignores Reflect. Confirm its special
attack ignores Light Screen and Aurora Veil. Repeat each situation with
Infiltrator temporarily turned off and confirm the protection works normally.

### Test I — Infiltrator and effect protection

Confirm Dragapult's Will-O-Wisp can burn an opponent through Safeguard. Confirm
Breaking Swipe can lower an opponent's Attack through Mist. Repeat both
situations with Infiltrator temporarily turned off and confirm Safeguard and
Mist block the effects normally.

### Test J — Infiltrator and Substitute

Confirm Dragapult's damaging and status moves reach the real opponent through
Substitute. Repeat with Infiltrator temporarily turned off and confirm
Substitute protects the opponent normally.

### Test K — Icy Rock

Have Cryogonal use Snowscape:

1. With Icy Rock active, confirm Snow lasts exactly eight turns.
2. Without Icy Rock, confirm Snow lasts exactly five turns.
3. Activate Magic Room before Snowscape and confirm the held Icy Rock has no
   effect, so Snow lasts exactly five turns.

In every case, confirm the remaining duration counts down and expires at the
correct time.

### Test L — Final closure

Confirm all six former catalog gaps now have real passing tests. Confirm all
previously completed battle behavior still passes. C11A may be marked complete
only after the complete validation and independent acceptance checks pass.
C11B remains complete and unchanged.

## Catalog Boundary

| Catalog family | Before | After |
|---|---:|---:|
| Pokemon species/forms | 8 | 11 |
| Moves | 62 | 64 |
| Abilities | 8 | 9 |
| Held items | 14 | 15 |
| Conditions | 40 | 40 |

No nature, type-chart, condition, display-name policy, or unrelated catalog
expansion is approved by this package.

## Affected Systems

| Area | Impact | Required result |
|---|---|---|
| Canonical proof catalog | Adds three Pokemon, two moves, one Ability, and one held item | Only the approved entries and display names are added |
| Ability behavior | Adds the official Infiltrator interaction | It works generically for its holder, not only for Dragapult or particular moves |
| Held-item and weather behavior | Adds Icy Rock's official Snowscape extension | It works generically for its holder and respects Magic Room |
| C11A checks | Replaces the six catalog deferrals with active proofs | Existing accepted checks remain present and continue passing |
| Earlier completed checks | The approved Ability, item, and catalog sets grow | Rerun the affected earlier checks before claiming no regression |
| Project status records | C11A may eventually change from incomplete to complete | Update status only after fresh validation and independent acceptance |

## Required Work Order

1. Lock and verify the approved source facts.
2. Add the generic Infiltrator and Icy Rock behavior through the existing
   battle-rule paths.
3. Add only the approved catalog records and display names.
4. Add Tests A through L inside the existing C11A test groups.
5. Run the affected earlier Ability, held-item, catalog, and battle checks.
6. Run the complete C11A and Battle validation.
7. Obtain a fresh independent acceptance review before changing C11A's status.

Implementation must stop and return for a new decision if any approved fact
cannot be represented without broadening this package.

## Acceptance Criteria

- [ ] Cryogonal, Rampardos, and Dragapult match the approved source facts.
- [ ] Waterfall and Breaking Swipe match every approved move fact and effect.
- [ ] Infiltrator ignores exactly the approved protections through ordinary
      Ability behavior; no move receives an invented bypass property.
- [ ] Icy Rock extends only its holder's use of Snowscape from five turns to
      eight and is disabled by Magic Room.
- [ ] Tests A through L pass through normal battle behavior.
- [ ] Every former C11A catalog deferral is replaced by an active proof.
- [ ] Existing battle checks continue to pass.
- [ ] C11A remains incomplete until fresh complete validation and independent
      acceptance both pass.
- [ ] C11B remains complete and unchanged.
- [ ] No unrelated content, visuals, audio, user interface, or game systems
      change.
- [ ] No Git staging, commit, push, branch, or history change occurs without
      separate explicit approval.

## Explicitly Excluded

- Hail.
- The full Pokedex or any Pokemon beyond the three approved subjects.
- Moves beyond Waterfall and Breaking Swipe.
- Abilities beyond Infiltrator.
- Light Clay, Damp Rock, Heat Rock, Smooth Rock, Terrain Extender, or any held
  item beyond Icy Rock.
- New condition types or changes to the type chart.
- Cry for Help, wild reinforcement, or later roadmap work.
- New battle presentation, visual assets, sprites, textures, animation, audio,
  user-interface design, or layout.
- A new replay format or unrelated battle-engine cleanup.
- Changes to completed C11B evidence.
- Git operations.

## GDD Update Required?

No. This package adds proof content for already-approved battle rules; it does
not change the Game Concept or establish a new game-wide rule.

Roadmap and session-status documents will require reconciliation only after
implementation, fresh validation, independent acceptance, and separate
approval to write those status changes.

## Source Snapshot Fingerprints

These fingerprints prove which official Pokemon Showdown files supplied this
package. They are recorded so later source changes cannot be mistaken for the
approved 2026-09-02 snapshot.

| Official file | Bytes | SHA-256 fingerprint |
|---|---:|---|
| `pokedex.json` | 523823 | `D9A21FCAEFCBBCC1CC2FE8CD2923CF29F190A42D1B024897B5102A075436345C` |
| `moves.json` | 490603 | `2F15B2BA099D1DDBCEAC95F594971C1747B9FBC690EDA4CB977721198860E9B3` |
| `abilities.js` | 107941 | `9FD639D3AC2DD8F65213D6393AFF46B53BB162A1716919DF82C53C62160DBA70` |
| `items.js` | 155675 | `1CADD51D7B00858820BC50B5E0F66C7719B8E59C185DD2A2B9161BB99C099A0B` |
| `learnsets.json` | 3190987 | `AAF035402D0455CEC88A5FB9363A43446419E3E71C575E4674D86ACC2A499058` |

## Approval and Implementation Boundary

The user approved this package and the catch-rate exception on 2026-09-02.
That approval authorizes this design document only. It does not authorize
source, test, catalog, asset, configuration, roadmap-status, or Git changes.

Implementation should begin in a fresh session with a separate exact-scope
approval because it changes shared Ability and held-item behavior and requires
substantial validation.
