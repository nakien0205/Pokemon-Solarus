# C11 — Full Integration and Release Gate

Priority: Mandatory completion gate  
Status: In progress; C10A, C10B, and C11B are complete and accepted. C11A's
test-only remediation is validated but remains `INCOMPLETE_CATALOG_DEFERRED`,
so full C11 remains open. The ADR-0002 implementation closeout is already PASS.

Default order: C11A, then C11B. The 2026-08-31 user exception permits C11B to
begin with six explicit C11A catalog-data gaps still deferred; it does not mark
C11A fully complete or waive their later retests.

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
scenario remained byte-identical. C11A began only after a separate
implementation-approval task and did not reinterpret C10B acceptance as C11
write authority.

## Objective

Prove that the complete bounded battle core works as one deterministic system,
not merely as passing isolated calculators. Produce evidence tied to exact live
source and data hashes.

## C11A — Deterministic Integration Matrix

Status: **INCOMPLETE_CATALOG_DEFERRED**. The approved remediation scope is
implemented and validated, but six production branches remain unverified
because the accepted catalog lacks the required data.

### Implemented file map and ownership

C11A is test-only. It uses the production imported catalog accepted by C10B
and the public Battle API; it adds no synthetic production definition or
private state-mutation hook.

| Implemented path | One responsibility |
|---|---|
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalIntegrationTestSupport.h` | Shared declarations for loading the production catalog, deterministic scenario builders, scripted decisions/RNG, replay comparison, and invariant assertions. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalIntegrationTestSupport.cpp` | The matching shared implementation; no test registration and no production ownership. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalSingleCoreIntegrationTests.cpp` | Single-Battle baseline, full flow, obedience, and switching integration. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalSingleEffectsIntegrationTests.cpp` | Single-Battle move, status, volatile, Bag, Ability, and held-item integration. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalModifierIntegrationTests.cpp` | B00B ordered damage trace plus Mold Breaker and Levitate public-engine comparison. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalConditionIntegrationTests.cpp` | Field, side, hazard, grounding, removal, expiry, and end-turn integration. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalDoubleIntegrationTests.cpp` | Double targeting, order, redirection, ally support, spread/friendly-fire, simultaneous faint, and modifier combinations. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalWildIntegrationTests.cpp` | Run, Capture, configured WildFlee, cancellation, and destination integration. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalPartnerIntegrationTests.cpp` | Partner selection visibility, ownership, continuation, victory, and recovery integration. |
| new `Game/Source/PokemonSolarus/Private/Tests/BattleCanonicalReplayInvariantTests.cpp` | Deterministic replay, serialization, snapshot/event/RNG equality, bounds, terminal immutability, and hidden-information invariants. |

The support pair is justified because the focused test families need the same
production-catalog loader and deterministic assertion machinery. Each test
source owns one scenario family. The 1,190-line support implementation remains
together under a task-specific organization exception: its fixture, selector,
replay/twin, coverage-manifest, and invariant responsibilities form one shared
integration-harness contract, while splitting would violate the exact-ten-file
boundary or create an arbitrary ownership cut. Review a focused split if an
independent responsibility is added, the ten-file constraint is lifted, or the
file approaches 2,000 lines.

No production C++, source JSON, Data Table, runtime scenario, display name,
configuration, `.uproject`, UI, or visual asset changed. Six catalog-data gaps
are declared below under the user-approved exception; no runtime capability gap
or second production owner was introduced.

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

Implemented prefixes:

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

### Implemented evidence and declared catalog gaps

The accepted remediation evidence is rooted at
`Game/Saved/AutomationReports/C11A-ReviewRemediation-20260831-221346`.

- Exactly ten C11A files and 25 Automation identities are present.
- The fresh forced-Unity build succeeded with linked DLL SHA-256
  `9432EE8A929878E27BC0A6A027A2116ED810EEAC2A540694102ACEC3FBBB2272`.
- Six serial filters passed exact success counts `4/10/4/6/5/25` with zero
  warning, failure, not-run, in-process, or per-test issue counters.
- Protected hashes matched `251/251` with zero mismatches.
- `code-review` was `APPROVED_WITH_SUGGESTIONS` with no required
  Critical/High/Medium change; `test-evidence-review` was `ADEQUATE` with zero
  blocking items for the approved remediation scope.
- `completion-audit.json` records the consolidated result, while
  `deferred-catalog-gaps.json` is the persistent later-test register.

C11A is not fully complete. The accepted catalog has no Ice species for Snow's
Defense branch, no Rock species for Sandstorm's Special Defense branch, no
damaging Water move for both weather branches, no damaging Dragon move for
Misty Terrain, no damaging or status move with side-protection bypass, and no
set-condition effect authored for an eight-turn duration. Each gap has a
data-arrival guard and required public-engine retest. The guard fails when the
needed data appears so the deferral cannot remain silent.

## C11B — Build, Automation, and Evidence Gate

Status: **COMPLETE — INDEPENDENT FINAL REVIEW PASS**. C11B writes no battle
behavior. The user explicitly permitted it to proceed while C11A remained
`INCOMPLETE_CATALOG_DEFERRED`. C11B preserved the six-gap register and does not
claim full C11 completion while any required deferred retest remains open.

Canonical C11B evidence is rooted at
`Game/Saved/AutomationReports/C11B-NormalUnityRemediation-20260901-081133`.

- Exactly 14 approved tracked C++ files changed. The remediation contains
  file-private helpers and test bodies in unique named namespaces and changes no
  battle behavior, public API, Automation identity, data, asset, configuration,
  RNG ownership, event order, or replay schema.
- The normal Editor build and exact forced-Unity build with
  `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA` both linked
  fresh DLLs. Final DLL SHA-256 is
  `906EA5B045F31677DE8015AB1FAB5D15B59D255B5CBA22F4C787A24A6092AA56`.
- Rebuilt source and binary identity discovery matched exactly at `418/418`
  with zero duplicates.
- All 29 serial reports were accepted at `836/836` aggregate, including
  `418/418` for the full `PokemonSolarus.Battle` prefix. Every exported warning,
  failure, error, not-run, in-process, and per-test issue counter was zero.
- Fresh C10B digest proof matched
  `94CDB260DD1129C61E80CF4087389F1DC0265E3DCEDF95E0E254EBBC6A7F3CBA`.
  Fresh C11A proof passed all 25 identities, six declared gap messages, and
  seven data-arrival guards while preserving `INCOMPLETE_CATALOG_DEFERRED`.
- Protected hashes matched `303/303` with exactly the approved 14 changes and
  zero unapproved changes. The canonical evidence index contains 86 files with
  verified hashes.

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

The required independent read-only Final Review completed on 2026-09-01. It
inspected the actual 14-file diff and canonical evidence, verified every C11B
claim above, found no unresolved Critical or High findings, and returned final
C11B verdict **PASS**. No missing in-scope mechanic, hidden untested branch,
stale canonical evidence, hidden coupling, nondeterminism, content-specific
branch, or scope creep was found. `C11A-DATA-001` through `C11A-DATA-006` remain
the explicit catalog-data exceptions and continue to block full C11 completion.

## Definition of Complete

The reusable Global Battle core is complete only when:

- B00 through C10 are complete with linked evidence.
- C11A's full matrix passes deterministically.
- C11B's build/full-suite/evidence gate passes.
- The independent review has no unresolved Critical or High findings.
- Remaining omissions are explicitly outside the approved bounded catalog, not
  silently unimplemented parts of an approved mechanic.

The special permission to implement C11B does not satisfy this definition by
itself. Full C11 completion remains blocked until the six declared C11A catalog
gaps are either exercised after accepted data arrives or are resolved by a new
explicit acceptance decision.

After acceptance, update only current roadmap/status documents with the final
evidence identities. Publication, commit, and push remain separate user-owned
Git decisions; passing C11B does not authorize them.
