# C05 — Hit, Damage, Reusable Effects, Fainting, and Outcomes

Priority: P1  
Status: C05A complete under focused validation; C05B not started
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
- Primary versus secondary effects and independent/shared chances as frozen in
  B00B.
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
