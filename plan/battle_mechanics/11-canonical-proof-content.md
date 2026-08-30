# C10 — Canonical Proof Content

Priority: P3  
Status: C10A source-row authoring complete under bounded static validation;
C10B import and integration is next

Required order: C10B, C11A, then C11B
Entry gate: Satisfied — ADR-0002 implementation PASS at
`b5db3e440d7c6eb5ba6ddbcc01a92a3c9b8756c0` with fresh affected-filter and
full-Battle evidence

R1-R6 resolved the generic target-vocabulary, redirection, ally action power,
hit-qualifier, weather-move, and held-item move-intent gaps for Swift, Fly,
Follow Me, Helping Hand, Thunder Wave, Powder moves, Toxic, Solar Beam, Thunder,
Knock Off, Trick, Thief, and Recycle. R6 also resolved B12 through typed optional condition-removal
semantics, making Rapid Spin, Defog, and Brick Break expressible without
move-specific runtime branches. Independent R7 passed against the live source
and exported evidence. The approved C10A write authored all 62 moves.

R6's final evidence root is
`Game/Saved/AutomationReports/R6-OptionalConditionRemoval-Final-20260830-160508`.
The focused removal filter passed 7/7; the seven serial affected filters passed
`42, 9, 8, 9, 20, 39, 18`. All issue counters are zero, final `code-review` was
APPROVED, and final `test-evidence-review` was ADEQUATE/COMPLETE. At R6
closeout, R7 had not yet been performed and no C10A row had been authored.

Independent R7 evidence is rooted at
`Game/Saved/AutomationReports/R7-IndependentGate-20260830-164042`. The live
75-file source manifest had zero mismatches. All 22 serial reports were clean,
with 746 overlapping successes over 390 unique full-Battle paths and full
Battle at 390/390. The requested forced-Unity target was up to date, so this
gate does not claim a fresh compiler action.

The reproducible pinned-source recipe and manifest are rooted at
`Game/Saved/AutomationReports/C10A-SourceContent-20260830-170739`. All frozen
raw-byte hashes matched, and the manifest contains exactly 8 species/forms,
25 natures, 62 moves, 8 Abilities, 14 items, and 40 conditions. Its canonical
manifest SHA-256 is
`721EE44EF5D0EF6B9522D4BA1A94546D7032D1F17E1CAE812D52C4EC8D986629`.
C10A implementation evidence is rooted at
`Game/Saved/AutomationReports/C10A-Implementation-20260830-172323`. The six
approved JSON files contain exactly 8 species/forms, 25 natures, 62 moves,
8 Abilities, 14 items, and 40 conditions. Static comparison against the pinned
manifest and descriptor/reference validation passed with zero errors. No Unreal
asset, importer, C++, or automated test was included. Git publication remains a
separate user-owned action.

## Completed R1–C10A implementation record

R1–R6 were remediation lanes for C10A, not replacements for C01–C09. Each
extended the earlier package that already owned the relevant contract. R7 was
an independent acceptance gate and changed no implementation.

| Lane | Published/evidence identity | Implemented result | Canonical owner |
|---|---|---|---|
| R1 | `06d884e` | Appended foe-only spread and selected-other target classes with stable queue, target, event, replay, adapter, and catalog handling. | C01A/B, C02B, C03A, C04A/B, C05B/C, C06B |
| R2 | `cf8b3e6` | Added typed Follow Me registration, private action-scoped state, proposal projection, lifecycle cleanup, replay/events, and atomic rollback. | C01B, C02B, C03A, C04B, C05B, C06, C07 lifecycle, C09 cleanup |
| R3 | `1294c7c` | Added typed Helping Hand registration with exact `3/2` magnitude, action binding, `BeforeDamage` application, cleanup, replay, and rollback. | C01B, C02B, C03A, C04/C05, C06, C07 lifecycle, C09 cleanup |
| R4A | `0f8664b` | Added reusable status-type-immunity, Powder, and Poison-user Toxic hit qualifiers in the established hit order. | C02B and C05A/B |
| R4B | `ef93d5d` | Added reusable Solar Beam charge/power and Thunder accuracy behavior through typed weather flags and existing field dispatch. | C02B, C05A/B, C07D |
| R5 | `7686395` | Added typed Knock Off, Trick, Thief, and Recycle item intents through the existing C08 ledger, hooks, reveal state, and event path. | C01B, C02B, C05B, C08A/C |
| R6 | `74b1c1b` | Added typed optional-absence removal for Rapid Spin, Defog, and Brick Break while preserving legacy failure and exact effect order. | C02B, C05B, C07C/D |
| R7 | no implementation commit | Independently accepted live source and exported evidence: 22 clean reports and full Battle 390/390. | C10 acceptance gate |
| C10A | `C10A-Implementation-20260830-172323` | Authored exactly six source JSON families and passed pinned-source/static validation with counts `8/25/62/8/14/40`. | C10A source content |

The detailed historical file lists and per-lane evidence hashes remain in
`docs/c10a-canonical-proof-content-implementation-ready-draft.md`. The current
package ownership and continuation contract live in this plan and the package
addenda under `plan/battle_mechanics`.

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

Status: **PLANNED — NOT IMPLEMENTED OR APPROVED**.

### Goal and discovered live dependency

C10B converts the accepted source rows into production Data Tables and proves
that the reflected rows build the same immutable catalog. It must not add new
battle mechanics.

The live runtime source requires a display name for every catalog species.
`display_names.json` currently has only Charizard and Venusaur, while C10A now
has eight species. Therefore the future C10B approval boundary must include the
six missing display-name rows and `DT_InitialBattleDisplayNames.uasset`; a
six-asset-only import cannot pass
`PokemonSolarus.Battle.Runtime.DataSource.ProductionAssets`.

### Planned hand-authored file map

| Path | One responsibility |
|---|---|
| new `Game/SourceData/Battle/battle_source_validation.py` | Pure, deterministic validation of all source families, stable IDs, enum names, ranges, references, effect shapes, counts, and the explicit import allowlist. It performs no Unreal mutation. |
| `Game/SourceData/Battle/import_initial_battle_data.py` | Two-phase Unreal import coordinator: complete preflight first, then mutate/save only the approved table allowlist, with in-memory rollback on failure. |
| `Game/SourceData/Battle/Initial/display_names.json` | Add only Gyarados, Rotom, Pelipper, Espathra, Clefable, and Excadrill display names required by the live runtime resolver. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalContentTests.cpp` | Focused imported-catalog inventory, source-equivalence, descriptor, diagnostics, frozen-catalog, and deterministic-summary evidence. |
| `Game/Source/PokemonSolarus/Private/Tests/BattleRuntimeDataSourceTests.cpp` | Replace the obsolete two-species/two-move catalog expectations with the complete imported catalog while retaining the existing runtime bundle, deterministic RNG, and fail-closed tests. |

No production C++ change is expected. If source preflight, reflected import,
the existing adapter/catalog, or runtime loader cannot support the approved
rows, C10B must stop and return to the owning package with a separately
approved implementation draft.

The new focused C++ test must remain one canonical-content family. If it grows
past the 500-line review trigger, split inventory/equivalence from invalid-input
and frozen-catalog tests rather than creating a mixed monolith. Shared helpers
belong in one focused private test-support pair only if at least two test files
need them.

### Planned source and asset boundary

Source JSON read/validate inputs:

- the six completed C10A files;
- existing `type_chart.json` and `runtime_scenario.json`; and
- the narrowly expanded `display_names.json`.

Approved future asset writes after complete preflight:

- `DT_InitialBattleSpeciesForms.uasset`
- `DT_InitialBattleNatures.uasset`
- `DT_InitialBattleMoves.uasset`
- `DT_InitialBattleAbilities.uasset`
- `DT_InitialBattleItems.uasset`
- `DT_InitialBattleConditions.uasset`
- `DT_InitialBattleDisplayNames.uasset`

Validate-only assets:

- `DT_InitialBattleTypeChart.uasset`
- `DT_BattleRuntimeScenario.uasset`

The importer must accept an explicit family allowlist. It may not silently
reimport all entries in its global `TABLES` tuple. The type chart and runtime
scenario bytes must remain unchanged unless a later approved draft expands
scope.

### Two-phase import contract

1. Record HEAD, dirty inventory, source JSON hashes, all nine Data Table hashes,
   `.uproject`, configuration, importer, and test hashes.
2. Run the pure validator outside Unreal. Require exact source counts, unique
   case-folded names, known fields/enums, valid ranges, complete references,
   compatible condition families/targets, ordered effects, and the pinned
   C10A manifest hash.
3. Start Unreal only after phase 2 passes. Resolve every required reflected row
   struct and prove every approved JSON document converts fully before changing
   an existing production table. The implementation may use temporary objects
   or an equivalent rollback-safe mechanism, but must demonstrate that a late
   conversion failure leaves production tables unchanged.
4. Load the seven approved target tables, verify their current row structs,
   replace rows in memory, and compare exact row-name sets.
5. On any error, restore every loaded table to its pre-import state and save
   nothing. On success, save exactly the seven approved tables together.
6. Build the catalog through `FBattleDataTableAdapter`, load the production
   runtime bundle, and run focused tests. A failure is not C10B acceptance and
   must not be hidden by process exit status.

Context7 did not expose detailed UE 5.8 Python Data Table APIs during this plan
update, so the future implementation draft must verify the exact temporary or
rollback API against the installed UE 5.8.1 Python stubs before writing code.

### Required focused evidence

New prefix: `PokemonSolarus.Battle.C10B.ImportAndCatalog`.

The focused suite must prove:

- exact counts: 8 species/forms, 25 natures, 62 moves, 8 Abilities, 14 items,
  40 conditions, 18 types, 324 type pairs, and 8 species display names;
- source JSON and imported reflected rows produce identical immutable values;
- every selected ID occurs exactly once and every reference resolves;
- all move flags, targets, held-item operations, condition families, chances,
  magnitudes, counts, duration/layer payloads, and effect order are preserved;
- the complex canonical sequences for Follow Me, Helping Hand, Solar Beam,
  Fly, Thunder, held-item moves, Rapid Spin, Defog, and Brick Break;
- duplicate, missing, wrong-family, unknown-enum, bad-range, bad-target, and
  malformed-effect fixtures fail atomically with row/field diagnostics;
- a catalog already copied into a battle is unchanged by later Data Table
  mutation/reimport; and
- a deterministic canonical catalog summary and SHA-256 are stable across two
  independent loads.

Update and run the three existing
`PokemonSolarus.Battle.Runtime.DataSource.*` tests. Run C02B, C04B, C05B,
C07B/C/D, C08B/C, C09B/C, and the new C10B prefix serially after the build.
Judge every run from exported `index.json`: discovered paths must match the
requested prefix exactly; succeeded must equal performed; warnings, failures,
not-run, in-process, and per-test issues must all be zero.

### C10B completion gate

C10B is complete only when:

- pure preflight and reflected conversion finish before production mutation;
- exactly seven approved assets changed and both validate-only assets retain
  their baseline hashes;
- imported catalog/runtime tests pass with no warning or invalid reference;
- protected source, config, project, and unrelated dirty paths retain hashes;
- `code-review` is APPROVED and `test-evidence-review` is ADEQUATE/COMPLETE;
- no move/species/Ability/item/condition ID branch or second catalog owner was
  introduced; and
- the status documents are updated only after the evidence is accepted.

Full integration scenarios remain C11A. Full build/suite and independent final
acceptance remain C11B.
