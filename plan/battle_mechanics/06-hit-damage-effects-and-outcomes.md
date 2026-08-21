# C05 — Hit, Damage, Reusable Effects, Fainting, and Outcomes

Priority: P1  
Status: C05A and C05B complete under focused validation; C05C not started
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
  checks. Damaging secondaries run after that target's damage events in
  descriptor order; `PerHit` secondaries repeat once per completed hit.
- Generic status, volatile, stat, field, side, and removal operations mutate
  only after an applied hook. Protect/charge/recharge/semi-invulnerability,
  switching, and item operations remain typed deferred hooks for later owners.
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
  `Game/Saved/Automation/C05B-EffectExecutor-20260821T061116Z/build-07.log`.
- The exact `PokemonSolarus.Battle.C05B` run discovered exactly nine tests:
  9 succeeded, 0 with warnings, 0 failed, and 0 not run. No non-C05B test was
  present in the authoritative JSON report:
  `Game/Saved/Automation/C05B-EffectExecutor-20260821T061116Z/report-04/index.json`.
- The matching editor log is
  `Game/Saved/Automation/C05B-EffectExecutor-20260821T061116Z/automation-04.log`.
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
| `Game/Source/PokemonSolarus/Public/Battle/BattleDefinitions.h` | `6439759209563ac93ee5311fabe5189c8f2268f9ed8278abcd200d0801d9ba50` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEvent.h` | `f01a954708d4dff62a685ae279eb39abdb44f41a43249d27785d9c1bc1000da9` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEngine.h` | `0dcc88024620cc6729905a2eeabdde8d51df57f6fa0fab4c3ec1e344feed093a` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.h` | `5670cd99f7a711199e405905ae2c0341cfb5bdccea2840a42997011758265754` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp` | `db257eb3f1706895e965cd4ff3699036be95ee104cbe7f6b143fd456fce5188d` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDefinitionCatalog.cpp` | `67c1b42330221444d630ba1f198c636dabb58b28477a79a292672445913bd6eb` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDataTableAdapter.cpp` | `d9a7e8afba80ba599403c17e900506e9ffd58b1b93b767e75fa5f9031943d679` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEvent.cpp` | `5929dbec194bf0a39f87667948579f24c728af67bdaaa72a40b31d3a942ecdc3` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp` | `70aaa599774e089d8487061fc0ab0a7a7d1d2fba72a1457da745b804f772cc52` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.h` | `1b0ca1938a4e3b34f0154f6b5eef9f491b2606bb72fc01dcbcd2e8abedcb08d2` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp` | `59fe96cc6f9241d014a1d51c266ce13947715d4eec5392a4d88ad161100cbc1f` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleEffectExecutorTests.cpp` | `b37e63e64d0b45f580b2bb09cd667836779625969196d770bdf57488a9b17a98` |

C05C is the next sequential package.
