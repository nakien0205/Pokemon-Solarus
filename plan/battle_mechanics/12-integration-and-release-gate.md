# C11 — Full Integration and Release Gate

Priority: Mandatory completion gate  
Status: Planned; C10A and C10B are complete and accepted; C11A is next but is
not implemented or approved; the ADR-0002 implementation closeout is already
PASS

Required order: C11A, then C11B

Accepted C10B import evidence is rooted at
`Game/Saved/AutomationReports/C10B-ImportAndCatalog-20260831-090312`; final
rebuild/review evidence is rooted at
`Game/Saved/AutomationReports/C10B-ReviewRemediationFinal-20260831-110350`.
The approved project-encoding conversion provenance is rooted at
`Game/Saved/AutomationReports/C10B-UProjectUtf8Normalization-20260831-095635`,
and C11A must begin from the UTF-8-without-BOM project SHA-256
`1F8CD7D128EDE4F1FA2B6D3D4E17DC0C748A7AC8C7C5B467234DF0C441BCCB17`. The
production catalog accepted for C11A has SHA-256
`94CDB260DD1129C61E80CF4087389F1DC0265E3DCEDF95E0E254EBBC6A7F3CBA`.
C10B imported exactly seven approved Data Tables; type chart and runtime
scenario remained byte-identical. C11A must begin in a fresh
implementation-approval task and must not reinterpret C10B acceptance as C11
write authority.

## Objective

Prove that the complete bounded battle core works as one deterministic system,
not merely as passing isolated calculators. Produce evidence tied to exact live
source and data hashes.

## C11A — Deterministic Integration Matrix

Status: **PLANNED — NOT IMPLEMENTED OR APPROVED**.

### Planned file map and ownership

C11A is expected to be test-only. It must use the production imported catalog
accepted by C10B and the public Battle API; it must not add synthetic
production definitions or private state mutation hooks.

| Planned path | One responsibility |
|---|---|
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalIntegrationTestSupport.h` | Shared declarations for loading the production catalog, deterministic scenario builders, scripted decisions/RNG, replay comparison, and invariant assertions. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalIntegrationTestSupport.cpp` | The matching shared implementation; no test registration and no production ownership. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalSingleIntegrationTests.cpp` | Single-Battle baseline, status, switching, Bag, item/Ability, faint, replacement, and terminal flows. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalDoubleIntegrationTests.cpp` | Double targeting, order, redirection, ally support, spread/friendly-fire, simultaneous faint, and modifier combinations. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalWildPartnerIntegrationTests.cpp` | Run, Capture, configured WildFlee, and Partner Double ownership/visibility/continuation flows. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalReplayInvariantTests.cpp` | Deterministic replay, serialization, snapshot/event/RNG equality, bounds, terminal immutability, and hidden-information invariants. |

The support pair is justified because four focused test families need the same
production-catalog loader and deterministic assertion machinery. Each test
source owns one scenario family. If any file reaches the 500-line review
trigger, split only along the named behavior boundary; do not create one C11A
test monolith.

No production C++, source JSON, Data Table, runtime scenario, display name,
configuration, `.uproject`, UI, or visual asset change is expected. A discovered
runtime capability gap must stop C11A and return to its owning package under a
new implementation approval.

### Execution sequence

1. Freeze HEAD, dirty inventory, production/test/data/asset hashes, linked DLL,
   imported catalog summary hash, and the exact C10B evidence root.
2. Load the production catalog through the accepted C10B path; do not rebuild
   canonical rows in test code.
3. Implement and run one scenario family at a time under its exact C11A prefix.
4. For each representative scenario, execute twice from the same catalog,
   setup, decisions, stat-refresh inputs, and scripted RNG.
5. Compare typed events, RNG trace, final snapshots, outcome, capture and item
   facts, and versioned replay bytes.
6. Run the full `PokemonSolarus.Battle.C11A` prefix only after all four focused
   families pass serially.

Planned prefixes:

- `PokemonSolarus.Battle.C11A.Single`
- `PokemonSolarus.Battle.C11A.Double`
- `PokemonSolarus.Battle.C11A.WildPartner`
- `PokemonSolarus.Battle.C11A.ReplayInvariants`

### Baseline regression

- Existing four calculator tests still pass unchanged.
- Charizard/Venusaur retains the documented base-damage fixtures.
- Final-damage tests clearly distinguish base result from later modifiers.

### Single Battle

- Full setup, selection, legality, order, PP, hit, damage, effects, faint,
  replacement, end-turn effects, and terminal outcome.
- Standard obedience at every eligibility/result boundary with exact RNG and
  action/PP behavior.
- Voluntary switching, trapping, forced/pivot switching, Shift accepted/
  declined, Set policy, and no reserve.
- Every major status and selected volatile, including cross-interactions.
- Bag actions, Ability/item triggers, ownership cleanup, and blocked actions.

### Double Battle

- Four active battlers, cross-side and same-side Speed ties, priority, Trick
  Room, Quick Claw, and locked-order stability.
- Selected, redirected, random, ally-support, spread/friendly-fire, empty,
  fainted, captured, and semi-invulnerable targets.
- Simultaneous spread HP changes/faints, stable event grouping, queued actions,
  and one/two mandatory replacements.
- Two voluntary switches with distinct reserves and duplicate-reserve rejection.

### Modifier combinations

- STAB, dual typing, immunity, critical, random damage, burn, spread, weather,
  terrain, screens, stages, Ability, and held-item phases in B00B order.
- Blaze/Overgrow thresholds, Rain/Sun, Levitate/Mold Breaker, Magic Guard,
  Focus Sash, Life Orb, Choice Band, Air Balloon, and screen removal.
- Exact `FDamageTrace`, RNG trace, HP result, reveal events, and cleanup.

### Conditions and field

- Every approved weather, terrain, hazard, screen, room, and side condition:
  start, replacement/coexistence, duration/layers, active effect, removal,
  expiry, and snapshot visibility.
- Entry hazards combined with type, grounded state, Levitate, Heavy-Duty Boots,
  Air Balloon, switching, and switch-in faint.
- No recursive/double trigger and deterministic end-turn ordering.

### Wild encounter

- Blocked Trainer Run and successful/failed custom wild escape, including exact
  counter persistence and RNG boundaries.
- Poke Ball capacity rejection, normal/critical failure/success, multiple
  captures, retained captured state, target/action cancellation, no PP, last
  capture Victory, and pending destination order.
- Cry for Help and wild reinforcement are **Freeze until call by user**. They
  are excluded from the current release gate; existing related code stays
  unchanged until explicitly requested.
- Configured wild-opponent flee with default-disabled policy, one/multiple wild
  opponents, authored probability, and OpponentFled outcome.

### Partner Double Battle

- Separate player/partner selectors, parties, Bags, actions, ownership, legal
  switches, and ally targets.
- Exact request sequence: human player choices first, partner AI after those
  choices, and enemy selectors using a filtered pre-choice observation.
- Enemy hidden-choice boundary and partner visibility allowance.
- Illegal partner capture/cross-owner switch/item.
- Player party exhaustion with partner continuation, Team Victory, and first
  valid player Pokemon restored to 1 HP/status cured.

### Replay and invariants

For every representative scenario, replay the same:

- Frozen catalog/settings/setup.
- Decision sequence.
- External stat refresh sequence.
- RNG sequence.

Require semantic equality of typed events, RNG trace, final snapshot, outcome,
capture results, and item-consumption facts. Also require byte-for-byte equality
of C01's versioned canonical `FBattleReplayRecord` serialization; never compare
raw in-memory struct bytes.

Global invariants:

- HP, PP, stage, duration, layers, and item counts remain in bounds.
- Permanent calculated stats are never mutated by transient modifiers.
- One accepted action executes at most once.
- Fainted/captured/removed battlers cannot act.
- Invalid/stale/canceled-before-execution paths consume only what their frozen
  rule explicitly permits.
- Terminal state is immutable.
- Hidden information never appears in unauthorized snapshots/events.

## C11B — Build, Automation, and Evidence Gate

Status: **PLANNED — NOT STARTED**. C11B writes no battle behavior. It begins
only after C11A is accepted against the imported C10B catalog.

Before running Unreal:

- Record source, test, JSON, Data Table/catalog summary, `.uproject`, and
  `DefaultEngine.ini` hashes.
- Confirm the intended test binary was rebuilt from current source.
- Choose unique timestamped report/log paths.

Verification order:

1. Build `PokemonSolarusEditor Win64 Development` normally.
2. Run the forced-Unity Editor build with the required self-contained-source
   flags.
3. Record the final linked DLL hash and discover/freeze the exact live test
   path manifest.
4. Run every completed package prefix serially from that final binary.
5. Run the complete `PokemonSolarus.Battle` Automation prefix.
6. Inspect exported `index.json`, not only process exit code or console text.
7. Classify test failures/warnings/not-run separately from unrelated optional
   engine/plugin startup warnings.
8. Compare all protected hashes after Unreal.

The package filters must run serially from the rebuilt live binary. At minimum:

- `PokemonSolarus.Battle.DamageCalculator`
- `PokemonSolarus.Battle.CoreContracts`
- `PokemonSolarus.Battle.C01B`
- `PokemonSolarus.Battle.C02A`
- `PokemonSolarus.Battle.C02B`
- `PokemonSolarus.Battle.C03A`
- `PokemonSolarus.Battle.C03B`
- `PokemonSolarus.Battle.C04A`
- `PokemonSolarus.Battle.C04B`
- `PokemonSolarus.Battle.C05A`
- `PokemonSolarus.Battle.C05B`
- `PokemonSolarus.Battle.C05C`
- `PokemonSolarus.Battle.C06A`
- `PokemonSolarus.Battle.C06B`
- `PokemonSolarus.Battle.C07A`
- `PokemonSolarus.Battle.C07B`
- `PokemonSolarus.Battle.C07C`
- `PokemonSolarus.Battle.C07D`
- `PokemonSolarus.Battle.C08A`
- `PokemonSolarus.Battle.C08B`
- `PokemonSolarus.Battle.C08C`
- `PokemonSolarus.Battle.C09A`
- `PokemonSolarus.Battle.C09B`
- `PokemonSolarus.Battle.C09C`
- `PokemonSolarus.Battle.Runtime`
- `PokemonSolarus.Battle.ADR0002`
- `PokemonSolarus.Battle.C10B`
- `PokemonSolarus.Battle.C11A`
- `PokemonSolarus.Battle`

Discover the rebuilt test set first and freeze an exact expected-path manifest;
historical counts must not be copied forward. For every exported `index.json`,
require discovered and performed counts to match that manifest, paths to belong
to the requested prefix, every path exactly once, succeeded equal performed,
and every aggregate/per-test warning, failure, error, not-run, and in-process
counter to be zero.

Completion requires:

- Zero failed tests.
- Zero not-run tests.
- Zero test warnings; unrelated engine warnings are documented separately.
- Successful Editor build from the live source.
- No stale generated source/object provides an absent implementation.
- No unapproved `.uproject` or configuration change.
- Evidence paths and source/data hashes recorded in this plan and the index.
- A normal Editor build and a forced-Unity Editor build with
  `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA`; the latter
  proves the new test files have self-contained includes and Unity-safe private
  names.
- The linked editor DLL hash recorded after the final build and unchanged for
  every accepted Automation report.
- Exact pre/post hashes for source JSON, all nine Data Tables, `.uproject`,
  `DefaultEngine.ini`, module rules, and preserved unrelated dirty files.

Do not delete old reports. Do not remove or rewrite the existing Android File
Server block without explicit approval. If Unreal changes protected files, stop
and ask before any cleanup.

## Final Review

Perform an independent read-only review against:

- Every approved scope item in `00-roadmap-index.md`.
- Every package acceptance criterion.
- The B00B modern-rules snapshot and explicit Solarus exceptions.
- The complete canonical content inventory.
- The public API boundary: no UI, progression, persistence, strategic AI, or
  World/Actor dependencies in core rules.

The reviewer must report missing mechanics, untested branches, stale evidence,
hidden coupling, nondeterminism, content-specific branches, and scope creep.

Run `code-review` and `test-evidence-review` in a fresh read-only session after
the final build/reports exist. Fix every validated in-scope finding, then
regenerate affected build and Automation evidence before acceptance. This
review is not replaced by green counters.

## Definition of Complete

The reusable Global Battle core is complete only when:

- B00 through C10 are complete with linked evidence.
- C11A's full matrix passes deterministically.
- C11B's build/full-suite/evidence gate passes.
- The independent review has no unresolved Critical or High findings.
- Remaining omissions are explicitly outside the approved bounded catalog, not
  silently unimplemented parts of an approved mechanic.

After acceptance, update only current roadmap/status documents with the final
evidence identities. Publication, commit, and push remain separate user-owned
Git decisions; passing C11B does not authorize them.
