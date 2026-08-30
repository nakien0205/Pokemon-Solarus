# C10 — Canonical Proof Content

Priority: P3  
Status: C10A row authoring not started; R1-R5 remediation is complete and R6
is next; ADR-0002 entry gate is satisfied

Required order: R6, R7, C10A, C10B, C11A, then C11B
Entry gate: Satisfied — ADR-0002 implementation PASS at
`b5db3e440d7c6eb5ba6ddbcc01a92a3c9b8756c0` with fresh affected-filter and
full-Battle evidence

R1-R5 resolved the generic target-vocabulary, redirection, ally action power,
hit-qualifier, weather-move, and held-item move-intent gaps for Swift, Fly,
Follow Me, Helping Hand, Thunder Wave, Powder moves, Toxic, Solar Beam, Thunder,
Knock Off, Trick, Thief, and Recycle. Those rows are now expressible but remain
unauthored. C10A stays blocked until R6 and the independent R7 gate pass.

## Objective

Populate the frozen rule framework with real canonical named definitions for
every approved mechanic family. Synthetic definitions remain useful for narrow
edge tests but cannot be the only evidence that the Data Table adapter and core
engine work together.

## Data Ownership and Paths

- Source JSON: `Game/SourceData/Battle`.
- Imported Data Table assets: `Game/Content/Data/Battle`.
- Row structures and import adapter: the Unreal-specific data layer from C02B.
- Plain definitions: immutable `FBattleDefinitionCatalog` owned by the core
  setup, never direct Data Table row pointers.

Use one JSON document/table per stable definition family: species/forms,
natures, moves, type chart, conditions, Abilities, held items, battle items,
and encounter actions. IDs are stable ASCII identifiers independent of display
names/localization.

## C10A — Required Canonical Rows

### Species/forms

- Charizard — Blaze.
- Venusaur — Overgrow.
- Gyarados — Intimidate.
- Rotom base form — Levitate.
- Pelipper — Drizzle.
- Espathra — Speed Boost.
- Clefable — Magic Guard.
- Excadrill — Mold Breaker.

Include canonical type, base stats, catch rate, and form identity required by
the proof scenarios. Do not add full Pokédex content.

### Natures and types

- All 25 canonical natures.
- All 18 canonical types.
- Complete 18x18 type chart.

### Baseline, order, and target moves

- Flamethrower.
- Vine Whip.
- Quick Attack.
- Swift.
- Earthquake.
- Follow Me.
- Helping Hand.

### Status, stage, and reusable-effect moves

- Swords Dance.
- Thunder Wave.
- Will-O-Wisp.
- Sleep Powder.
- Ice Beam.
- Poison Powder.
- Toxic.
- Confuse Ray.
- Bite.
- Protect.
- Leech Seed.
- Wrap.
- Mean Look.
- Taunt.
- Encore.
- Disable.
- Substitute.
- Solar Beam.
- Hyper Beam.
- Fly.
- Thunder.
- Bullet Seed.
- Giga Drain.
- Double-Edge.
- Recover.

### Switching, item, and removal moves

- U-turn.
- Roar.
- Knock Off.
- Trick.
- Thief.
- Recycle.
- Rapid Spin.
- Defog.
- Brick Break.

### Weather and terrain moves

- Sunny Day.
- Rain Dance.
- Sandstorm.
- Snowscape.
- Electric Terrain.
- Grassy Terrain.
- Misty Terrain.
- Psychic Terrain.

### Hazards, screens, rooms, and side-condition moves

- Spikes.
- Toxic Spikes.
- Stealth Rock.
- Sticky Web.
- Reflect.
- Light Screen.
- Aurora Veil.
- Trick Room.
- Magic Room.
- Wonder Room.
- Tailwind.
- Safeguard.
- Mist.

### Solarus and engine-supplied moves

- Cry for Help and wild reinforcement are **Freeze until call by user**. Keep
  existing related code unchanged and add no proof content until explicitly
  requested.
- WildFlee is an engine action enabled only by an explicit encounter/species
  policy; include a synthetic configured encounter fixture, not a default
  canonical species assignment.
- Struggle is an engine-supplied immutable fallback definition, not a normal
  selectable Data Table move.

### Conditions

- Major: Burn, Paralysis, Sleep, Freeze, Poison, Badly Poisoned.
- Volatile: Confusion, Flinch, Protect chain, Leech Seed, partial trap,
  switch-prevention trap, Taunt, Encore, Disable, Substitute, ChargingTurn,
  RechargeTurn, and SemiInvulnerable.
- Weather: Harsh Sunlight, Rain, Sandstorm, Snow.
- Terrain: Electric, Grassy, Misty, Psychic.
- Hazards: Spikes, Toxic Spikes, Stealth Rock, Sticky Web.
- Screens: Reflect, Light Screen, Aurora Veil.
- Rooms: Trick, Magic, Wonder.
- Side: Tailwind, Safeguard, Mist.

### Abilities

- Blaze, Overgrow, Intimidate, Levitate, Drizzle, Speed Boost, Magic Guard,
  Mold Breaker.

### Held items

- Leftovers, Sitrus Berry, Lum Berry, Focus Sash, Life Orb, Choice Band,
  Heavy-Duty Boots, Air Balloon, Quick Claw.

### Battle items

- Poke Ball, Hyper Potion, Revive, Full Heal, X Attack.

## C10B — Import, Cross-Reference, and Behavior Validation

Import workflow:

1. Validate JSON syntax and stable IDs before opening Unreal.
2. Import/reimport into the matching Data Table row structure.
3. Build one complete catalog through `FBattleDataTableAdapter`.
4. Reject the complete catalog if any row/reference is invalid.
5. Serialize a deterministic catalog summary/hash for test evidence.
6. Run direct-definition tests and Data-Table-import integration tests.

Required cross-reference checks:

- Every species type/Ability exists.
- Every move type, target, flag, and effect definition exists and is compatible.
- Every condition/Ability/item references supported trigger phases and typed
  effect operations.
- Every move has valid PP, category, power/accuracy convention, priority, and
  target class.
- Every selected mechanic has at least one canonical activation path and one
  success, failure/block, and cleanup/expiry test where applicable.
- Content values match the pinned B00B reference snapshot.

Canonical mechanics may require small rule flags, but C10 must not add bespoke
branches such as `if MoveId == Flamethrower`. If the framework cannot express a
row, report the missing generic capability and return to the owning package.

## Tests

- JSON and Data Table import produce the same immutable values as direct
  definitions.
- Reimport invalidation cannot change a catalog already frozen for a battle.
- Duplicate/missing/wrong-family references fail atomically with row/field
  diagnostics.
- All 25 natures, 18 types, 324 type pairs, named species, moves, conditions,
  Abilities, and items are present exactly once.
- Every canonical move executes through effect descriptors, not ID branches.
- Charizard/Venusaur retains the current numeric base-damage regression while
  later final-damage scenarios use complete rules.

## Acceptance

- The complete proof catalog imports without warnings or invalid references.
- Every approved mechanic family has real data and deterministic behavior
  evidence.
- No full Pokédex/move/item catalog or unrelated content is added.
- C11 can build all integration scenarios using this catalog without synthetic
  production substitutions.
