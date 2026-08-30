# C05 — Hit, Damage, Reusable Effects, Fainting, and Outcomes

Priority: P1  
Status: C05A, C05B, and C05C complete under focused validation
Required order: C05A, C05B, then C05C

## Objective

Resolve validated move actions through exact hit and damage math, execute
ordered data-driven effects, and establish deterministic faint and terminal
checkpoints. Preserve the current calculator as the base-damage stage.

## C05A — Accuracy, Critical Hits, and Final Damage

Keep `FBattleDamageCalculator::TryCalculateDamage` and its four tests as the
generic base calculation. Move category identity may live in a shared header,
but current callers remain source-compatible.

Add pure staged services for:

- Accuracy/evasion and always-hit checks.
- Critical-hit eligibility, stage/chance, and stage-ignoring behavior.
- Base damage from the current calculator.
- Every B00B modifier phase, including random roll, STAB, type effectiveness,
  burn, spread, weather, screens, terrain where applicable, Ability hooks,
  held-item hooks, and minimum/zero results.

Rules:

- Follow B00B's exact operation and rounding order; do not combine rational
  modifiers into an unverified floating-point expression.
- Emit `FDamageTrace` with each named intermediate integer/result.
- Immunity produces zero damage and a typed no-effect result.
- Successful non-immune damage respects the documented minimum.
- Do not consume a critical, random-damage, or secondary roll if execution
  ended before that stage.
- Guard every integer operation and reject impossible overflow rather than
  wrapping.

## C05B — Reusable Effect Executor

Execute the ordered effect descriptors supplied by the move definition:

- Damage and per-target spread resolution.
- Major/volatile status and stat changes through typed future hooks.
- Fixed/percentage healing, drain, recoil, and self-damage.
- Fixed/ranged multi-hit count and one complete damage event per hit.
- Primary versus secondary effects and independent chances as frozen in B00B;
  B00B defines no shared-chance grouping mechanism.
- Charging, recharge, Protect-like blocking, semi-invulnerability, field/side
  creation/removal, switching, and item operations through typed hooks.

Execution rules:

- Consume PP at the documented start-of-execution point.
- Resolve multi-hit attacks one hit at a time. Stop after a hit faints the
  target and report only completed hits.
- Damage and the target's required faint checkpoint precede linked drain or
  recoil unless B00B records a move-specific exception.
- Damaging moves apply status/stat secondary effects after damage feedback
  events.
- End-of-turn effects cannot interrupt an executing multi-hit action.
- Every blocked, immune, failed, capped, or prevented effect emits a typed
  result and does not emit a false success mutation.

## C05C — Faint, Removal, and Outcomes

Add deterministic checkpoints:

1. After each damaging hit.
2. After linked effects such as recoil/drain.
3. After the complete action.
4. After all queued actions that must finish before replacement.
5. During ordered end-of-turn triggers.
6. Before requesting replacements or ending the battle.

Rules:

- A battler reaching zero HP emits HP change, faint, and removal eligibility in
  stable order.
- A battler fainting before its queued action cannot act and consumes no PP.
- Opponent-removal checkpoints occur only after linked effects and final faint
  state are known.
- Multiple simultaneous zero-HP results share a simultaneous-group ID and then
  emit stable individual faint events.
- Simultaneous final fainting resolves as player Defeat.
- Terminal outcomes are Victory, Defeat, Escape, ScriptedEnd, or Abandoned,
  with capture and partner Team Victory represented as typed causes.
- Terminal state rejects every later decision.
- C07/C08 later fill the end-of-turn trigger list without replacing these
  checkpoints.

## Tests

C05A:

- Externally sourced golden vectors for exact rounding and modifier order.
- Accuracy/evasion extremes, always-hit, misses, critical eligibility, random
  damage extremes, STAB, dual types, immunity, burn, spread, weather/screen
  hook inputs, minimum damage, and overflow rejection.
- Exact RNG call counts for hit, critical, and random damage.
- Existing base fixtures remain 90/24 and 44/22 where currently specified.

C05B:

- Primary/secondary success and failure, stage cap failure, healing cap, drain,
  recoil, multi-hit early faint, Protect, charge/recharge, and semi-invulnerable
  reachability.
- No duplicated effect from recursion or event replay.

C05C:

- Faint before action, recoil double faint, simultaneous spread faint, last
  opponent removed, player wiped, partner still alive, and terminal rejection.
- Stable opponent-removal checkpoint for future same-resolution EXP handling.
- Deterministic event order and replay for every case.

## Acceptance

- The base calculator remains a separately tested base stage.
- Every final damage result includes a trace sufficient to diagnose rounding.
- One accepted action executes at most once.
- No fainted battler acts, no HP escapes bounds, and no terminal battle accepts
  another command.
- C06 can add switching/replacement without altering damage-stage contracts.

## C05A Completion Evidence

C05A completed on 2026-08-21. It added pure, engine-independent accuracy,
critical-hit, and final-damage services plus eight tests under the exact
`PokemonSolarus.Battle.C05A` filter.

Frozen implementation boundaries:

- `FBattleDamageCalculator::TryCalculateDamage` and its four existing tests
  remain unchanged and authoritative for the base stage. B00B's explicit OF32
  operations apply to the ordered post-base damage phases implemented here.
- The documented minimum is applied at B00B step 9 before its required OF16.
  Therefore an exact pathological OF16 wrap can return numeric zero while the
  typed result remains `Damage`; immunity remains a distinct typed `NoEffect`.
- Host-language overflow is rejected before RNG. B00B's explicit OF32 and OF16
  operations are intentional rules and are not treated as host overflow.
- C05A exposes only pure staged calculation and named hook inputs. It does not
  integrate actions, mutate HP, execute move effects, emit battle events, or
  begin C05B/C05C.

Final validation:

- Forced-unity `PokemonSolarusEditor Win64 Development` build succeeded with
  adaptive exclusions disabled and four build actions completed:
  `Game/Saved/Logs/C05A-EditorBuild-ForcedUnity-20260821T050136Z.log`.
- The exact `PokemonSolarus.Battle.C05A` run performed eight tests: 8 succeeded,
  0 with warnings, 0 failed, and 0 not run; process exit code 0. Evidence is in
  `Game/Saved/Automation/C05A-HitDamage-20260821T050211Z/` and
  `Game/Saved/Logs/C05A-HitDamage-20260821T050211Z.log`.
- Per the user's explicit validation limit, no older battle filter and no full
  `PokemonSolarus.Battle` suite was run. Optional platform SDK and profiler DLL
  startup messages did not produce Automation warnings or failures.
- Module rules, `.uproject`, `DefaultEngine.ini`, the unchanged base calculator
  and tests, B00B snapshot, and Solarus interview handoff matched their pre-run
  hashes after validation.

Implemented C05A source hashes:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleHitResolver.h` | `82143e8785861c0b215b8753a470a96e56488d398faff9145e61b8051ecda886` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleHitResolver.cpp` | `a1723110af9f463d43d065dbb360b2cd468df3eab8fd5ec849389eba8bac88d6` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleFinalDamageCalculator.h` | `5fff7169192dac38ddda4b7c56c598043eb3f095c513fe1e9a25be574b3d450e` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleFinalDamageCalculator.cpp` | `30aded7b2e0b57867ad726c8cc46223bae28aff67476089902aaede1af11fbf1` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleHitDamageTests.cpp` | `e8d88a39ced7aec2d9a3d3b0e986463f90a5493753a3a60a5e3404d5d0460dde` |

## C05B Completion Evidence

C05B completed on 2026-08-21. It added the private reusable effect executor,
the public engine execution step, and exactly nine tests under the
`PokemonSolarus.Battle.C05B` filter.

Frozen implementation boundaries:

- Existing enum ordinals and replay schema 4 remain unchanged.
  `RemoveCondition` and the C05B effect-outcome events are appended values.
- Primary `1/1` descriptors consume no chance draw. Every eligible explicit
  `1..100/100` secondary consumes an independent `U[0,99]`, including
  `100/100`. No shared-chance group was added.
- A move may define one fixed `2..5` or ranged `2..5` multi-hit descriptor.
  It is primary, precedes the sole damage descriptor, and is rejected for a
  spread move. Ranged count uses B00B's exact `U[0,19]` mapping.
- The executor prevalidates the complete request before RNG or mutation,
  stages all mutable state, and publishes no partial result on failure. Each
  reached hit performs independent critical and damage draws, applies HP and
  immediate-update hooks, and reports the actual completed hit count.
- Healing, drain, recoil, fixed self-damage, and Struggle recoil use the frozen
  magnitude meanings with half-up rounding, HP caps, and positive-source
  checks. Authored percentage magnitudes cannot exceed one whole, so unsafe
  payloads fail validation before chance RNG. Fixed amounts still use a
  denominator of one.
- Non-`PerHit` spread secondaries run in stored target and descriptor order only
  after all successful spread damage. Action-scoped `User`, `UserSide`,
  `TargetSide`, `BothSides`, and `Field` effects apply once per concrete target;
  explicit `PerHit` secondaries still repeat once per completed hit.
- A `BothSides` effect expands to both sides even when its move reached one side.
  `TypelessDamage` is reserved for engine-owned definitions such as Struggle
  and is rejected by the authored catalog and Data Table adapter.
- Generic status, volatile, stat, field, side, and removal operations mutate
  only after an applied hook. Protect/charge/recharge/semi-invulnerability,
  switching, and legacy fixed-`ItemId` item changes remain typed deferred hooks
  for later owners. R5's authored held-item operation variants instead produce
  staged C08 ledger intents for remove, exchange, transfer, and restore.
- `FBattleEngine::ExecuteCurrentMoveEffects()` accepts only the current
  committed and successfully targeted Fight action and enforces
  `Pending -> Executing -> Completed` exactly once. Rejected duplicate,
  re-entrant, premature, or invalid calls do not apply effects or consume
  effect RNG.
- Zero HP sets the existing faint and pending-transition state and blocks later
  locked actions. C05B does not emit `Fainted`, remove battlers, request
  replacements, or resolve outcomes; those remain C05C.

Final validation:

- The forced-unity `PokemonSolarusEditor Win64 Development` build succeeded
  with `-ForceUnity -DisableAdaptiveUnity -NoUBA`:
  `Game/Saved/Automation/C05B-EffectExecutor-Fixes-20260821T083903Z/build.log`.
- The exact `PokemonSolarus.Battle.C05B` run discovered exactly nine tests:
  9 succeeded, 0 with warnings, 0 failed, and 0 not run. No non-C05B test was
  present in the authoritative JSON report:
  `Game/Saved/Automation/C05B-EffectExecutor-Fixes-20260821T083903Z/report/index.json`.
- The matching editor log is
  `Game/Saved/Automation/C05B-EffectExecutor-Fixes-20260821T083903Z/automation.log`.
  The process exited 0, but completion was determined from the JSON summary and
  individual entries.
- Per the user's explicit validation limit, no C05A, older battle filter, or
  full `PokemonSolarus.Battle` suite was run.
- Module rules, `.uproject`, `DefaultEngine.ini`, every pre-existing test,
  C05A sources/tests, B00B, and the Solarus interview handoff matched their
  recorded pre-run hashes after Unreal exited.

Implemented C05B source hashes:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleDefinitions.h` | `9bb707f6aa00bbfeb0a73a3ea284584d9afad2303d625e4a8eaa7399bce2f7da` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEvent.h` | `f01a954708d4dff62a685ae279eb39abdb44f41a43249d27785d9c1bc1000da9` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEngine.h` | `0dcc88024620cc6729905a2eeabdde8d51df57f6fa0fab4c3ec1e344feed093a` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.h` | `5670cd99f7a711199e405905ae2c0341cfb5bdccea2840a42997011758265754` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp` | `885dc70b2a6ae7450d416d394a500986c3b14aa47e9f5a8b1a212f0b8ed555c0` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDefinitionCatalog.cpp` | `a1eeddab47938bff938a7830e44dabbe2e2dcc4d17dac84240065c09bb0f9d8d` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDataTableAdapter.cpp` | `41f5bc6f60589c096448ce29a424f855853b6f95a4f6a6d16708e9f880a51825` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEvent.cpp` | `5929dbec194bf0a39f87667948579f24c728af67bdaaa72a40b31d3a942ecdc3` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp` | `70aaa599774e089d8487061fc0ab0a7a7d1d2fba72a1457da745b804f772cc52` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.h` | `1b0ca1938a4e3b34f0154f6b5eef9f491b2606bb72fc01dcbcd2e8abedcb08d2` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp` | `59fe96cc6f9241d014a1d51c266ce13947715d4eec5392a4d88ad161100cbc1f` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleEffectExecutorTests.cpp` | `2153b7fac2398273451dd5c9cb972e026c594dfaacf50455b9622d03266baf2d` |

## C05C Completion Evidence

C05C completed on 2026-08-21 from the clean `c7d944a` baseline. It added one
private faint/outcome resolver, automatic engine integration, and exactly seven
public-engine tests under the `PokemonSolarus.Battle.C05C` filter.

Frozen implementation boundaries:

- `FBattleEngine::ExecuteCurrentMoveEffects()` now emits each zero-HP
  `Damage -> HPChanged -> Fainted` sequence before linked recoil. Direct
  spread faints share one nonzero resolution-scoped simultaneous-group ID and
  retain stable Player/Opponent and Left/Right order.
- Only after every linked effect is known, fainted battlers lose major status,
  temporary stat stages, and volatiles; their active slots are vacated and
  `LeftActiveSlot`, `Removed`, and applicable
  `OpponentRemovalCheckpoint` facts are emitted in stable order.
- `ActionCompleted` precedes terminal resolution. Both sides losing their
  final usable Pokemon in one action produces `Defeat / SimultaneousFaint`;
  the other completed outcome branches are ordinary Victory, ordinary Defeat,
  and `Victory / PartnerTeamVictory`.
- A fainted queued actor is canceled without PP or execution RNG. Queue
  exhaustion publishes stable `ReplacementRequired` slot facts when a living
  reserve exists, otherwise enters `EndOfTurn`; C05C does not select or perform
  a replacement.
- Impossible disagreement between already committed effect state and the
  private faint resolver terminates through an all-configuration `Fatal`
  invariant, so Test/Shipping cannot continue with partial faint cleanup.
- The existing one-use between-actions stat-refresh seam remains restricted to
  the `Resolving` phase. C05C emits the future progression checkpoint fact but
  does not implement EXP, level progression, or terminal progression
  orchestration.
- No public method, enum, event ordinal, replay field, or schema was added.
  Replay schema 4 and the existing hit and simultaneous-group metadata remain
  authoritative. C06 switching/replacement selection and C07/C08 mechanics
  remain later work.

Review and final validation:

- The required parallel Unreal/C++ and QA-testability reviews first identified
  the all-configuration invariant guard and assertion gaps. After patching,
  both reviews were rerun and reported no remaining actionable findings.
- The final forced-unity `PokemonSolarusEditor Win64 Development` build
  succeeded with `-ForceUnity -DisableAdaptiveUnity -NoUBA`:
  `Game/Saved/Logs/C05C-Final-EditorBuild-ForcedUnity-20260821T092416Z.log`.
- The exact `PokemonSolarus.Battle.C05C` run discovered exactly seven tests:
  7 succeeded, 0 with warnings, 0 failed, 0 not run, and 0 in process. Every
  individual test entry contains 0 warnings and 0 errors:
  `Game/Saved/Automation/C05C-Final-20260821T092434Z/report/index.json`.
- The matching editor log is
  `Game/Saved/Automation/C05C-Final-20260821T092434Z/automation.log`.
- Each required test executes its scenario twice and compares total event
  order, RNG trace, outcome and cause, and canonical replay bytes. The seven
  tests also cover cleanup/removal order, slot vacancy, replacement payload
  and order, the no-replacement `EndOfTurn` branch, full spread hit/group
  metadata, checkpoint one-use, and terminal/replacement exclusivity.
- Per the user's explicit validation limit, no C05B, C05A, older battle
  filter, or full `PokemonSolarus.Battle` suite was run. No fresh runtime claim
  is made for those filters.
- Module rules, `.uproject`, `DefaultEngine.ini`, every C05A source/test, the
  unchanged C05B executor/support sources, B00B, and the Solarus interview
  handoff matched their recorded hashes after final validation.

Implemented C05C source hashes:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Private/Battle/BattleFaintOutcomeResolver.h` | `60b68d3b9aa5269d56ea2edbc49003382319d9c64881b57a111327e265f98088` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleFaintOutcomeResolver.cpp` | `28844517aca2b8eb39dd3a537f7661de33b885631a62523df03021063b926baa` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp` | `53df7ee3c62927ab9b54125c6ee16c887c62fa5762aa20c47d6ad970e8644c84` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleFaintOutcomeTests.cpp` | `999fc1d7305318ae264b9c855394e8e96f14f0abdcbac7c9f2f15cd9b1979c2c` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleEffectExecutorTests.cpp` | `2a9f96992aeab114dbb113d52682dd12e88773461c2baa2c760be47cdb8607dc` |

C05 is complete under focused validation. C06A is the next sequential package.

## R4B live executor addendum — 2026-08-29

The separately approved R4B lane replaced the private boolean charge hook with
`TryShouldSkipEffectDescriptor(Effect, OutShouldSkip)`. The charge owner caches
one staged first-turn decision, preserves charged release, and treats weather
dispatch failure as `InvalidHookResult`. The accuracy owner dispatches canonical
weather at the existing `BeforeAccuracy` phase; Rain is literal always-hit with
no accuracy draw, while Sun uses ordinary accuracy RNG at base accuracy `50`.
Failures use the existing hit-resolution rollback path.

Every actual damage build reuses the existing `BeforeDamage` weather dispatch.
Rain, Sandstorm, and Snow append the named Q12 `2048` power modifier after the
existing ally-action priority 10 and terrain priority 6 modifiers. The final
damage calculator and its rounding are unchanged. One staged context, the outer
identity recheck, transactional RNG, state application, publication, replay
schema, PP owner, and target-lock owner remain unchanged.

## R6 optional condition-removal addendum — 2026-08-30

R6 resolved B12 with the typed `OptionalIfAbsent` effect flag. The flag is
accepted only on `RemoveCondition`; using it on another effect type is invalid.
An optional removal still consumes its normally authored secondary-effect
chance. If the named condition is absent, the descriptor succeeds silently,
stages no mutation, emits no condition event or update, and continues to later
effects. A legacy removal without the flag keeps the existing failure behavior.

The executor determines presence from the catalog definition and the staged
battler, side, or field collection for the descriptor's exact owner and
condition family. A present condition is removed through its normal cleanup
path exactly once. Primary removal descriptors ordered before `Damage` retain
the existing pre-damage checkpoint used by Brick Break. Secondary removals
retain the connected-hit gate, including hits routed into Substitute, used by
Rapid Spin. Protect still blocks both routes unless an independently authored
rule says otherwise; R6 did not reuse `BreaksProtection`.

Final evidence is rooted at
`Game/Saved/AutomationReports/R6-OptionalConditionRemoval-Final-20260830-160508`.
The focused `PokemonSolarus.Battle.C05B.C10Removal` filter passed 7/7, followed
serially by C05B, C07B, C07C, C07D, C08B, C08C, and ADR0002.3E6 with counts
`42, 9, 8, 9, 20, 39, 18`. All aggregate and per-test issue counters are zero.
Final `code-review` was APPROVED and `test-evidence-review` was
ADEQUATE/COMPLETE after their validated findings were fixed.

## Consolidated C10A effect-pipeline record — 2026-08-30

The C05 pipeline owns the runtime side of every R1–R6 remediation lane:

| Lane | C05-owned behavior |
|---|---|
| R1 | Accepts the two appended target shapes without changing existing target or faint behavior. |
| R2 | Executes one typed redirection registration through staged action state. |
| R3 | Registers an exact rational ally modifier and applies it at `BeforeDamage` before terrain. |
| R4A | Evaluates typed status-type-immunity, Powder, and Poison-user reach/accuracy qualifiers in the established hit order. |
| R4B | Applies authored weather charge, accuracy, and power rules without move-ID branches. |
| R5 | Converts final typed item descriptors into staged C08 ledger intents after a connected hit and before later switch/recoil work. |
| R6 | Treats only authored optional absent removals as silent success while preserving normal chance and effect ordering. |

All lanes preserve one staged executor context and the outer
`BattleEngineMoveEffects.cpp` identity recheck, RNG commit/rollback, state
application, and event publication. The focused prefixes are
`C10Redirection`, `C10ActionModifiers`, `C10HitRules`,
`C10WeatherMoveRules`, `C10HeldItemMoves`, and `C10Removal` under their recorded
C04B/C05B/C08C owners. Independent R7 accepted the combined code and evidence;
C10A then authored rows only and changed no executor source.
