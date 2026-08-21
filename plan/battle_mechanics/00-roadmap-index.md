# Global Battle Mechanics Roadmap

Status date: 2026-08-21  
Roadmap status: Approved and materialized; B00 through C05 complete under focused validation
Next package: C06A in a new session; its dependencies are clear

## Current Truth

The live battle source now contains:

- `FPokemonBattleStats`, containing Max HP, Attack, Defense, Special Attack,
  Special Defense, and Speed.
- `FBattleDamageCalculator::TryCalculateDamage`, which calculates only the
  deterministic Physical or Special base-damage stage.
- Four `PokemonSolarus.Battle.DamageCalculator.*` Automation tests.
- C01A's strong battle/definition/slot identifiers, frozen setup enums,
  resolved nature modifier, injectable seeded RNG contract, and seven
  `PokemonSolarus.Battle.CoreContracts.*` Automation tests.
- C01B's validated immutable setup, typed decision/rejection contracts, sole
  mutable engine owner, immutable snapshots, ordered event/resolution model,
  canonical replay record/serializer, and nine
  `PokemonSolarus.Battle.C01B.*` Automation tests.
- C02A's validated permanent-stat inputs and calculations, invariant-safe
  `FBattleStatStages`, pure effective-stat and accuracy/evasion queries, and
  seven `PokemonSolarus.Battle.C02A.*` Automation tests.
- C02B's exact 18x18 type chart, plain definition records and ordered move
  effects, atomic immutable-by-interface catalog, Unreal Data Table copy
  adapter, and seven `PokemonSolarus.Battle.C02B.*` Automation tests.
- C03A's single authoritative private battle state, frozen catalog-backed
  Trainer/battler/party/active records, typed mechanic-state storage, invariant
  validator, existing-snapshot projections, and six
  `PokemonSolarus.Battle.C03A.*` Automation tests.
- C03B's observer-filtered deep snapshots, typed visible/unavailable decision
  options, stable human/partner/enemy request sequence, atomic Left/Right
  batches, typed effectiveness knowledge, replay schema 2 projection, and six
  `PokemonSolarus.Battle.C03B.*` Automation tests.
- C04A's validated normal-turn selections, engine-owned Struggle fallback,
  immutable ordered queue, Solarus command/Speed/tie keys, deterministic
  obedience gate, replay schema 3 projection, and seven
  `PokemonSolarus.Battle.C04A.*` Automation tests.
- C04B's ten target classes, exact four-position structural model, typed
  battler/side/field target sets, capture and fainted-target timing, stable
  random targeting, replay schema 4 projection, and seven
  `PokemonSolarus.Battle.C04B.*` Automation tests.
- C05A's pure accuracy and critical stages, unchanged authoritative base-damage
  calculator, exact ordered final-damage modifiers, typed immunity results,
  named damage trace, checked arithmetic, and eight
  `PokemonSolarus.Battle.C05A.*` Automation tests.
- C05B's prevalidated reusable effect executor, exact gate/RNG/effect order,
  staged HP/condition mutation, healing/drain/recoil, fixed/ranged multi-hit,
  appended effect outcomes and removal operation, exact-once public engine
  step, pending-faint boundary, and nine `PokemonSolarus.Battle.C05B.*`
  Automation tests.
- C05C's automatic faint/removal/outcome continuation, target-before-recoil
  faint order, stable simultaneous spread groups, cleanup and slot vacancy,
  one-use opponent-removal checkpoints, queued-fainted-actor cancellation,
  replacement/end-turn boundaries, terminal outcomes, and seven
  `PokemonSolarus.Battle.C05C.*` Automation tests.

There is now one authoritative internal battle-state owner and a deterministic
normal-turn selection, queue-lock, action-start, and final-target seam, plus the
frozen public setup/decision/event/snapshot/replay, stat, type, move,
definition, adapter, and pure hit/damage language needed by later packages.
There is still no switching/replacement selection system, concrete condition
behavior engine, encounter flow, or presentation seam. The
completed Story 001 and its 33-test report describe an older source state and
are historical evidence only. The current Git history begins with initial commit
`d302018d4cd7d11a40b55c2003e164345b5011f7`, after the numeric-only state
already existed, so it cannot explain or restore those missing files.

## B00 Execution Status

- B00A: Complete on 2026-08-20, run
  `B00A-DamageCalculator-20260820T090904Z`.
- B00B: Complete and accepted on 2026-08-20. Its accepted snapshot is
  `plan/battle_mechanics/reference/modern-rules-snapshot.md`, SHA-256
  `ded20d707ab67c2bb4d883df8ac07cbd20e38a31766b8a9ef14515c93168ad50`.
- `Q-B00B-01` is resolved. The user authorized the narrow supplementary Gen IX
  source set, then approved explicit Solarus closures for the rules the sources
  still did not establish.
- The B00 through C05 gates are clear. C06A is dependency-clear; every later
  package remains blocked or not started. The workspace's sequential default
  makes C06A the next session.
- B00A verified installed UE 5.8.1, changelist `56057345`, and a successful
  `PokemonSolarusEditor Win64 Development` target evaluation.
- The focused calculator run discovered and passed exactly four tests: 4
  succeeded, 0 with warnings, 0 failed, and 0 not run; process exit code 0.
- Stale objects for absent battle systems remain in `Intermediate` as required,
  but none is named by the current module linker response and none supplied an
  extra runtime test.
- Git was present at the execution baseline on clean `main` at the initial
  commit above. B00A performed no Git write or commit action.

Protected B00A hashes, unchanged before and after Unreal:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleStats.h` | `92028991c761de61439c37d5e006121194f42c19b10021185b6157ec168518de` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleDamageCalculator.h` | `6b4951eba72e3782d392fdf16cfd7f4dc27227843def6a8af019e959d717fe38` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDamageCalculator.cpp` | `f9a61783d19d37dfc7f931d2eaf4f381a1fb52ab06360d3fe209a77326fdc7c5` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleDamageCalculatorTests.cpp` | `c8c32253ff4332ee745d79a897d34c4a23b7d6e43bf456923fc428bc8bd35db1` |
| `Game/PokemonSolarus.uproject` | `97d07ae09b7fbcb7e095ebfd5a4a15c1e1c2953e40e93ec2c89bf407257af7e5` |
| `Game/Config/DefaultEngine.ini` | `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920` |

Fresh local evidence:

- Manifest:
  `Game/Saved/Automation/B00A-DamageCalculator-20260820T090904Z/b00a-evidence.json`.
- Automation report:
  `Game/Saved/Automation/B00A-DamageCalculator-20260820T090904Z/index.json`.
- Automation log:
  `Game/Saved/Logs/B00A-DamageCalculator-20260820T090904Z.log`.
- Editor build log:
  `Game/Saved/Logs/B00A-EditorBuild-20260820T090904Z.log`.
- Full counters, artifact hashes, source inventory, Git transition note,
  stale-object containment, and separate startup-noise findings are recorded in
  the package file and manifest.
- The pinned Showdown, damage-calc, and capture commits were not consulted by
  B00A. They were fetched at their exact revisions and consulted by the current
  B00B pass.

B00B accepted evidence:

- Pokemon Showdown revision
  `34caa98811fd6ed5d2f173ec1fc29dd9bd4bc91d`.
- Smogon damage-calc revision
  `83807801012f0af3e2dbb543d6fd40b483b3ebab`.
- Scarlet 1.1.0 capture parent revision
  `31d6aa136883ab354f5e8151526ee40f07317be0`, plus ten child revisions and
  their SHA-256 content hashes recorded in the snapshot.
- Solarus handoff SHA-256
  `476040bcaf0cfdb9d7f97d3fba3ddc752f7760de252bcc7a8775f846a58c98a6`.
- Installed UE 5.8.1 `DataTable.h` SHA-256
  `1644b15509198b370716c3a106193f3bc642f40ab646235526c69fb8db4a73a4`;
  Epic's UE 5.8 Data Table pages were checked as the adapter authority.
- Supplementary Gen IX raw-page pins and SHA-256 values: Obedience
  `oldid=4554429` / `b53f050d074c5c9e58d9364754f4ea1a2cd0c63449a8db0f8e70ab2576d2d97f`;
  Priority `oldid=4535978` / `2502da8f8cadeca76ff9b2d3fbe916cb83b77629279251751928ca32624214b4`;
  Revive `oldid=4594350` / `e650ef2f9d71ebf9be981d5f21485b61e11c90ba7197cc20ca774b5ab0c1d81a`;
  Full Heal `oldid=4614360` / `eec2a5f9863c102e070df707c942949a0e3e7de0c51bdf7135fccc57b0875549`;
  X Attack `oldid=4595515` / `056ec4a271f7b73f81e13525877e2d167856bccf63689e0ab6605fa97eebf31d`.
- Diagnostic obedience artifacts were also pinned: SacredPhoenix PDF SHA-256
  `1d53fcb0e807cc386abeb633d6d5556573b87dbe06831082ab2d22673356e7f1`
  and Smogon discussion HTML SHA-256
  `a6f868f74c20043b1538a104d482d8570683b8cec01b3bc101be00e7c11c3a31`.
- Accepted Solarus closures are deterministic above-cap refusal before PP/RNG;
  `Run > voluntary switch > Bag/capture > moves`; Revive HP
  `max(1, floor(MaxHP / 2))`; and X Attack rejection at `+6` without consuming
  its item or action. Full Heal cures all frozen major statuses plus Confusion.
- No battle source, `.uproject`, or Unreal configuration file was changed by
  B00B. C01 was not started.
- With user authorization, `.gitignore` stopped broadly hiding `docs/`,
  `design/`, `src/`, `plan/`, and `production/`, making the durable work
  visible to Git. Generated `Saved/`, `Intermediate/`, `Binaries/`, logs, and
  `production/session-logs/` remain ignored. Current `.gitignore` SHA-256 is
  `0f1da0b6bf5ba95c2514d19b036f0e2ee7a8d899c2530bf898a9e5d8f13f24e9`.

## C01A Execution Status

- C01A completed on 2026-08-20 with successful run ID
  `C01A-CoreContracts-20260820T122558Z`.
- The `PokemonSolarusEditor Win64 Development` build succeeded after compiling
  all four new C01A translation units, with 0 build warnings and 0 build
  errors.
- `PokemonSolarus.Battle.CoreContracts` discovered and passed exactly seven
  tests: 7 succeeded, 0 with warnings, 0 failed, and 0 not run; process exit
  code 0.
- The protected `PokemonSolarus.Battle.DamageCalculator` regression filter
  discovered and passed exactly four tests: 4 succeeded, 0 with warnings, 0
  failed, and 0 not run; process exit code 0.
- Per the user's latest instruction, the full `PokemonSolarus.Battle` suite was
  not run. C01B's focused suite was also not run because C01B was not started.
- The protected calculator source/tests, module rules, `.uproject`, and
  `DefaultEngine.ini` hashes were unchanged before and after Unreal. The config
  remained at SHA-256
  `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920`.
- The JSON reports contain no test warnings or failures. Separate startup noise
  was optional profiler/GPU-capture/tablet DLL messages, unavailable EOS
  anti-cheat, one stale-layout migration warning in the CoreContracts run, and
  Unreal's own `UE::UnifiedErrorTest` diagnostics; no Automation or Solarus
  battle issue line was found.
- Successful evidence is under
  `Game/Saved/Automation/C01A-CoreContracts-20260820T122558Z/`,
  `Game/Saved/Automation/C01A-DamageCalculator-20260820T122558Z/`, and the
  matching `Game/Saved/Logs/C01A-*-20260820T122558Z.log` files.
- This is the preserved C01A handoff. C01B's completed status is recorded
  below.

## C01B Execution Status

- C01B completed on 2026-08-20 with successful run ID
  `C01B-Contracts-20260820T131414Z`.
- The first source-compiling Editor build
  (`C01B-EditorBuild-20260820T131146Z`) found 13 local C01B compiler errors and
  0 warnings. The fixes stayed inside C01B files. The subsequent
  `PokemonSolarusEditor Win64 Development` build
  (`C01B-EditorBuild-20260820T131322Z`) succeeded with 0 compiler warning lines
  and 0 compiler error lines.
- `PokemonSolarus.Battle.C01B` discovered and passed exactly nine tests: 9
  succeeded, 0 with warnings, 0 failed, and 0 not run; process exit code 0.
- The protected `PokemonSolarus.Battle.CoreContracts` regression filter passed
  exactly seven tests, and `PokemonSolarus.Battle.DamageCalculator` passed
  exactly four tests. Both runs had 0 warnings, 0 failures, and 0 not-run tests;
  both processes exited 0.
- The full `PokemonSolarus.Battle` filter discovered and passed exactly 20
  tests: 20 succeeded, 0 with warnings, 0 failed, and 0 not run; process exit
  code 0.
- The C01A sources/tests, calculator sources/tests, module rules, `.uproject`,
  and `DefaultEngine.ini` matched their pre-run hashes after Unreal. The config
  remained at SHA-256
  `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920`;
  no Android File Server cleanup was needed.
- All four JSON reports record 0 test warnings and 0 failures. Separate startup
  noise is limited to optional profiler/GPU-capture/tablet DLL messages and
  Unreal's own `UE::UnifiedErrorTest` diagnostics; no Automation or Solarus
  battle issue line was found.
- Successful evidence is under
  `Game/Saved/Automation/C01B-Contracts-20260820T131414Z/`,
  `Game/Saved/Automation/C01B-C01A-CoreContracts-20260820T131508Z/`,
  `Game/Saved/Automation/C01B-DamageCalculator-20260820T131555Z/`, and
  `Game/Saved/Automation/C01B-FullBattle-20260820T131636Z/`, with matching
  timestamped files under `Game/Saved/Logs/`.
- C01 is complete. C02 and C03 have now completed in the later focused sessions
  recorded below. C04A is the default next package in the shared sequential
  workspace.

## C02A Execution Status

- C02A completed on 2026-08-20 with focused run ID
  `C02A-Stats-20260820T134951Z`.
- The `PokemonSolarusEditor Win64 Development` build
  (`C02A-EditorBuild-20260820T134917Z`) succeeded after compiling the new
  calculator/stage runtime and test units, with 0 compiler warning lines and 0
  compiler error lines.
- `PokemonSolarus.Battle.C02A` discovered and passed exactly seven tests: 7
  succeeded, 0 with warnings, 0 failed, and 0 not run; process exit code 0.
- Per the user's explicit validation limit, only the C02A-focused filter was
  run. The protected `PokemonSolarus.Battle.DamageCalculator` filter and the
  full `PokemonSolarus.Battle` suite were not rerun. Their source/test hashes
  remained unchanged, but this session does not claim fresh runtime regression
  evidence for them.
- `BattleSetupTypes.h`, the base-damage implementation/tests, module rules,
  `.uproject`, and `DefaultEngine.ini` matched their pre-run hashes after
  Unreal. `BattleStats.h` changed intentionally for C02A inputs. The config
  remained at SHA-256
  `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920`;
  no Android File Server cleanup was needed.
- The focused JSON report records no warnings or failures. Separate startup
  noise was limited to optional profiler/GPU-capture/tablet DLL messages,
  unavailable EOS anti-cheat, and Unreal's own `UE::UnifiedErrorTest`
  diagnostics; no C02A or Automation issue line was found.
- Successful evidence is under
  `Game/Saved/Automation/C02A-Stats-20260820T134951Z/`, with logs
  `Game/Saved/Logs/C02A-Stats-20260820T134951Z.log` and
  `Game/Saved/Logs/C02A-EditorBuild-20260820T134917Z.log`.
- C02A is complete under the approved focused-validation scope. This was its
  handoff point; C02B's later completion is recorded immediately below.

## C02B Execution Status

- C02B completed on 2026-08-20 with focused run ID
  `C02B-Definitions-20260820T143547Z`.
- The final `PokemonSolarusEditor Win64 Development` build
  (`C02B-EditorBuild-20260820T143528Z`) succeeded with no compiler warning or
  error line.
- The exact `PokemonSolarus.Battle.C02B` filter discovered and performed seven
  tests: 7 succeeded, 0 with warnings, 0 failed, and 0 not run; process exit
  code 0.
- Tests cover every B00B type-chart cell and dual products; required move
  record shapes; catalog rejection, atomicity, and deterministic ordering;
  reflected table validation; JSON import; deep copying; and proof that later
  source-row mutation cannot affect the frozen catalog.
- Per the user's explicit validation limit, only the C02B-focused filter was
  run. C01, C02A, base-damage, and full `PokemonSolarus.Battle` filters were not
  rerun. Their relevant source/test hashes remained unchanged, but this session
  does not claim fresh runtime regression evidence for them.
- The first build (`C02B-EditorBuild-20260820T143205Z`) exposed one ignored
  `[[nodiscard]]` result. The first focused run
  (`C02B-Definitions-20260820T143414Z`) exited 1 after two passing tests because
  a test fixture appended an element from its own `TArray`. Both corrections
  stayed inside C02B files; the later build and full seven-test focused run are
  the acceptance evidence.
- The successful JSON report has no warnings or errors. Separate startup noise
  was limited to optional profiler/GPU-capture/tablet DLL messages, unavailable
  EOS anti-cheat, and Unreal's own `UE::UnifiedErrorTest` diagnostics; no C02B
  or Automation issue line was present.
- C01/C02A/base-damage sources and tests, module rules, `.uproject`, the B00B
  snapshot, Solarus handoff, and `DefaultEngine.ini` matched their pre-run
  hashes. The config remained at SHA-256
  `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920`;
  the Android File Server block was unchanged.
- Successful evidence is under
  `Game/Saved/Automation/C02B-Definitions-20260820T143547Z/`, with logs
  `Game/Saved/Logs/C02B-Definitions-20260820T143547Z.log` and
  `Game/Saved/Logs/C02B-EditorBuild-20260820T143528Z.log`.
- C02 is complete under the approved focused-validation scope. C03A's later
  completion is recorded immediately below.

## C03A Execution Status

- C03A completed on 2026-08-21 with focused run ID
  `C03A-State-20260821T002222Z`.
- The final `PokemonSolarusEditor Win64 Development` build
  (`C03A-EditorBuild-20260821T002200Z`) succeeded with no compiler warning or
  error line.
- The exact `PokemonSolarus.Battle.C03A` filter discovered and performed six
  tests: 6 succeeded, 0 with warnings, 0 failed, and 0 not run; process exit
  code 0.
- Tests cover valid Single, Double, and Partner Double state creation;
  structural party and active-position storage; Trainer ownership and action
  allowances; atomic invalid-setup rejection; runtime HP, PP, stage, resource,
  lifecycle, and frozen-catalog reference invariants.
- The first build (`C03A-EditorBuild-20260821T001849Z`) exposed one local use of
  an unavailable `TArray` helper. The first focused run
  (`C03A-State-20260821T001944Z`) then passed five tests before a test fixture
  appended an element from its own `TArray` and triggered an assertion. Both
  corrections stayed within C03A files; the later build and six-test run are
  the acceptance evidence.
- Per the user's explicit validation limit, only the C03A-focused filter was
  run. C01, C02, base-damage, and full `PokemonSolarus.Battle` filters were not
  rerun, so this session makes no fresh runtime claim for them.
- C01/C02 contracts and tests, module rules, `.uproject`, and
  `DefaultEngine.ini` matched their pre-run hashes. The config remained at
  SHA-256
  `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920`.
- Successful evidence is under
  `Game/Saved/Automation/C03A-State-20260821T002222Z/`, with logs
  `Game/Saved/Logs/C03A-State-20260821T002222Z.log` and
  `Game/Saved/Logs/C03A-EditorBuild-20260821T002200Z.log`.
- C03A is complete under the approved focused-validation scope. C03B's later
  completion is recorded immediately below.

## C03B Execution Status

- C03B completed on 2026-08-21 with focused run ID
  `C03B-SnapshotDecision-20260821T010229Z`.
- The final `PokemonSolarusEditor Win64 Development` build
  (`C03B-EditorBuild-20260821T010213Z`) succeeded. The exact
  `PokemonSolarus.Battle.C03B` filter discovered and performed six tests: 6
  succeeded, 0 with warnings, 0 failed, and 0 not run; process exit code 0.
- Tests prove deep copied observer snapshots; no hidden opponent reserve, Bag,
  PP, Ability, held-item, or unexecuted-choice leakage; stable Single, Double,
  Partner Double, and player-controls-partner sequencing; atomic Left/Right
  batches; catalog-backed legal options and typed unavailable reasons;
  typed known/unknown/varied effectiveness; stale rejection without gameplay
  version or RNG change; and the between-actions stat-refresh seam.
- A post-pass review corrected form-level known-species matching and made event
  definition reveals family-aware. The final build and focused run above are
  the acceptance evidence.
- Per the user's explicit limit, no C01, C02, C03A, base-damage, or full
  `PokemonSolarus.Battle` runtime filter was run. This session makes no fresh
  runtime claim for those suites.
- Module rules, `.uproject`, `DefaultEngine.ini`, the B00B snapshot, and the
  Solarus interview handoff matched their pre-run hashes after Unreal. Exact
  source and protected-file hashes are recorded in the C03 package handoff.
- Successful evidence is under
  `Game/Saved/Automation/C03B-SnapshotDecision-20260821T010229Z/`, with logs
  `Game/Saved/Logs/C03B-SnapshotDecision-20260821T010229Z.log` and
  `Game/Saved/Logs/C03B-EditorBuild-20260821T010213Z.log`.
- C03 is complete under the approved focused-validation scope. C04A's later
  completion is recorded immediately below.

## C04A Execution Status

- C04A completed on 2026-08-21 with final focused run ID
  `C04A-Actions-Exit-20260821T015813Z`.
- The final `PokemonSolarusEditor Win64 Development` module build
  (`C04A-EditorBuild-Suffixed-20260821T015247Z`) succeeded. The exact
  `PokemonSolarus.Battle.C04A` filter discovered and performed seven tests: 7
  succeeded, 0 with warnings, 0 failed, and 0 not run; process exit code 0.
- The engine now validates complete selections and freezes normal-turn actions
  by Solarus command band, move/fractional priority, effective Speed, and exact
  cross-side/same-side tie rules. It also supplies typed zero-PP options,
  engine-owned Struggle, action-start revalidation, deterministic obedience,
  PP-at-commit behavior, core-only order events, and replay schema 3.
- C04A freezes generic `+0.1` and reverse-Speed hook inputs only. C08C still owns
  Quick Claw eligibility, RNG, and reveal; C07D owns Trick Room state. At C04A
  completion, C04B had not begun; its later completion is recorded below.
- The original C04A execution honored the user's validation limit and ran only
  the C04A-focused filter. A separately approved cleanup on 2026-08-21 then
  repaired the older duplicate enum and file-local helper declarations without
  changing any of the 11 validated C04A source/test files.
- The cleanup passed both a full forced-unity Editor build with adaptive
  exclusions disabled (`C04A-UnityCleanup-Pass3-20260821T022230Z.log`) and a
  normal default adaptive-unity Editor build
  (`C04A-UnityCleanup-DefaultAdaptive-20260821T022251Z.log`). No permanent unity
  override was added; the build configuration remains empty.
- Focused cleanup reports under
  `Game/Saved/Automation/C04A-UnityCleanup-20260821T022425Z/` cover the affected
  older filters plus unchanged C04A: 44 succeeded, 0 with warnings, 0 failed,
  and 0 not run. This was not a full `PokemonSolarus.Battle` suite run.
- Module rules, `.uproject`, `DefaultEngine.ini`, the B00B snapshot, and the
  Solarus interview handoff matched their pre-run hashes after the final test.
  Exact source and protected-file hashes are recorded in the C04 package handoff.
- Successful evidence is under
  `Game/Saved/Automation/C04A-Actions-Exit-20260821T015813Z/`, with logs
  `Game/Saved/Logs/C04A-Actions-Exit-20260821T015813Z.log` and
  `Game/Saved/Logs/C04A-EditorBuild-Suffixed-20260821T015247Z.log`.
- C04A is complete under the approved focused-validation scope. C04B's later
  completion is recorded immediately below.

## C04B Execution Status

- C04B completed on 2026-08-21 with final focused run ID
  `C04B-Targeting-Final3-20260821`.
- The final normal adaptive non-unity and forced-unity
  `PokemonSolarusEditor Win64 Development` builds succeeded. The exact
  `PokemonSolarus.Battle.C04B` filter discovered and performed seven tests:
  7 succeeded, 0 with warnings, 0 failed, and 0 not run; process exit code 0.
- The target resolver now owns the ten C04B target classes over exactly four
  canonical structural positions. It rejects empty, fainted, captured, and
  removed positions for new selection, keeps living semi-invulnerable
  battlers selectable, excludes the user from fixed spread sets, and produces
  typed battler, side, and field targets in stable order.
- Target class and the final typed target set are frozen on the locked action
  for C05. Capture cancels before PP, selected-opponent fainting falls back to
  the other living opponent after PP, and random legal opponents consume the
  injected targeting draw only for a non-empty candidate set, including the
  one-candidate `U[0,0]` case. Doubles Struggle requires an explicit opponent.
- `TargetsResolved` events validate their target-class shape and serialize in
  replay schema 4. Identical setup, decisions, and seed produced identical
  queue, targeting event, RNG trace, and canonical replay bytes.
- The seven C04B tests use only public battle APIs. Per the user's package
  boundary, no older or future Automation filter was run. The one existing
  C04A assertion made stale by the required replay schema-4 bump was updated
  from 3 to 4, but the C04A filter was not run; this completion therefore makes
  no fresh runtime claim for that filter.
- Ordinary redirection-proposal generation remains with the later Ability and
  condition owners; C04B freezes and tests the ordered legal proposal seam.
  Public engine regressions for a switched replacement occupying a selected
  position and for the post-PP no-legal-target completion path remain deferred
  until a later package exposes those state transitions without private-state
  test access. The production resolver and engine paths for both cases are
  present; C05 hit reachability and damage remain out of scope.
- Module rules, `.uproject`, `DefaultEngine.ini`, the B00B snapshot, and the
  Solarus interview handoff matched their pre-run hashes after validation.
  Exact source and protected-file hashes are recorded in the C04 package
  handoff.
- Successful evidence is under
  `Game/Saved/Automation/C04B-Targeting-Final3-20260821/`, with logs
  `Game/Saved/Logs/C04B-Targeting-Final3-20260821.log`,
  `Game/Saved/Logs/C04B-EditorBuild-Final3-20260821.log`, and
  `Game/Saved/Logs/C04B-EditorBuild-ForcedUnity-Final3-20260821.log`.
- C04 is complete under the approved focused-validation scope. C05A's later
  completion is recorded immediately below.

## C05A Execution Status

- C05A completed on 2026-08-21 with final focused run ID
  `C05A-HitDamage-20260821T050211Z`.
- The forced-unity `PokemonSolarusEditor Win64 Development` build succeeded
  with adaptive exclusions disabled. The exact `PokemonSolarus.Battle.C05A`
  filter discovered and performed eight tests: 8 succeeded, 0 with warnings,
  0 failed, and 0 not run; process exit code 0.
- The new pure services resolve literal/numeric accuracy, modern critical
  stages and blocking, critical stage/screen ignores, the unchanged current
  base calculator, and the exact B00B post-base modifier order. All random
  draws use the injected action-scoped RNG and every final damage result carries
  a named integer trace.
- Focused vectors cover all 16 random factors, accuracy and critical extremes,
  externally pinned rounding/order, real dual-type chart products, typed
  immunity and typeless bypass, burn and its explicit exception, spread,
  weather, STAB, screens, terrain/Ability/item hook inputs, minimum and defined
  OF16 zero behavior, and true host-overflow rejection.
- The current base calculator and its four tests were not changed. C05A retains
  that calculator as the authoritative base stage; explicit OF32/OF16 behavior
  in later B00B phases remains distinct from rejected host-language overflow.
- C05A did not touch engine integration, actions, PP, HP, effects, events,
  replay, fainting, or outcomes. Per the user's explicit validation limit, no
  older battle filter and no full `PokemonSolarus.Battle` suite was run.
- Module rules, `.uproject`, `DefaultEngine.ini`, the unchanged base calculator
  and tests, B00B snapshot, and Solarus interview handoff matched their pre-run
  hashes after validation. Exact C05A source hashes are recorded in the C05
  package handoff.
- Successful evidence is under
  `Game/Saved/Automation/C05A-HitDamage-20260821T050211Z/`, with logs
  `Game/Saved/Logs/C05A-HitDamage-20260821T050211Z.log` and
  `Game/Saved/Logs/C05A-EditorBuild-ForcedUnity-20260821T050136Z.log`.
- C05A is complete under the approved focused-validation scope. C05B's later
  completion is recorded immediately below.

## C05B Execution Status

- C05B completed on 2026-08-21; its reviewed fixes have final focused report
  `C05B-EffectExecutor-Fixes-20260821T083903Z/report`.
- The forced-unity `PokemonSolarusEditor Win64 Development` build succeeded
  with `-ForceUnity -DisableAdaptiveUnity -NoUBA`. The exact
  `PokemonSolarus.Battle.C05B` filter discovered exactly nine tests: 9
  succeeded, 0 with warnings, 0 failed, and 0 not run; process exit code 0.
- The private executor validates the complete request before drawing or
  mutating, stages engine-owned state, preserves B00B's gate and per-target
  order, and applies ordered damage, multi-hit, secondaries, healing, drain,
  recoil, generic condition/stat/field/side operations, and typed future hooks.
- Primary `1/1` effects consume no chance draw. Eligible explicit
  `1..100/100` secondaries each consume independent `U[0,99]`, including
  `100/100`; no shared-chance mechanism was added.
- Non-`PerHit` spread secondaries wait until every successful spread target has
  taken damage. Action-scoped effects are de-duplicated per concrete target,
  while explicitly per-hit effects keep their authored repetition.
- `BothSides` now expands from a single reached side. Oversized authored
  percentages and the engine-owned `TypelessDamage` flag are rejected before
  effect RNG or mutation.
- The public engine step is exact-once after commitment and C04B targeting.
  Zero HP sets the existing faint/pending-transition facts and blocks later
  locked actions, but C05B emits no faint/removal/replacement/outcome behavior.
- Replay schema 4 and all earlier enum ordinals remain unchanged. Public C05B
  event kinds and `RemoveCondition` were appended.
- Per the user's explicit validation limit, no C05A, older battle filter, or
  full `PokemonSolarus.Battle` suite was run. The JSON report contains no
  non-C05B test, warning, error, duplicate path, failure, or not-run entry.
- The remediation changed three private C05B implementation files, one public
  move-flag comment, its existing test file, and these C05B records. It did not
  edit module/configuration files, C05A, B00B, or the Solarus interview handoff.
  Exact C05B source hashes are recorded in the C05 package handoff.
- Final evidence is
  `Game/Saved/Automation/C05B-EffectExecutor-Fixes-20260821T083903Z/build.log`,
  `Game/Saved/Automation/C05B-EffectExecutor-Fixes-20260821T083903Z/automation.log`,
  and
  `Game/Saved/Automation/C05B-EffectExecutor-Fixes-20260821T083903Z/report/index.json`.
- C05B is complete under the approved focused-validation scope. C05C's later
  completion is recorded immediately below.

## C05C Execution Status

- C05C completed on 2026-08-21 from the clean `c7d944a` baseline.
- The private faint/outcome resolver is integrated automatically through
  `FBattleEngine::ExecuteCurrentMoveEffects()`; no optional faint API, public
  enum, event ordinal, replay field, or replay-schema change was added.
- The final forced-unity `PokemonSolarusEditor Win64 Development` build
  succeeded with `-ForceUnity -DisableAdaptiveUnity -NoUBA`:
  `Game/Saved/Logs/C05C-Final-EditorBuild-ForcedUnity-20260821T092416Z.log`.
- The exact `PokemonSolarus.Battle.C05C` report contains exactly seven tests:
  7 succeeded, 0 with warnings, 0 failed, 0 not run, and 0 in process. Every
  individual entry also contains 0 warnings and 0 errors:
  `Game/Saved/Automation/C05C-Final-20260821T092434Z/report/index.json`.
- The matching editor log is
  `Game/Saved/Automation/C05C-Final-20260821T092434Z/automation.log`.
- The required parallel Unreal/C++ and QA-testability reviews were rerun after
  patching; both final reviews reported no remaining actionable findings.
- Per the user's explicit validation limit, no C05B, C05A, older battle
  filter, or full `PokemonSolarus.Battle` suite was run. No fresh runtime claim
  is made for those filters.
- Module rules, `.uproject`, `DefaultEngine.ini`, C05A sources/tests, the
  unchanged C05B executor/support files, B00B, and the Solarus interview
  handoff matched their recorded hashes after final validation.
- C05 is complete under the approved focused-validation scope. C06A is the
  next sequential package; C06 switching and replacement selection were not
  implemented by C05C.

## Goal

Build a deterministic, plain-C++ battle core using Scarlet/Violet-style modern
rules plus explicit Solarus exceptions. The finished core supports:

- Single, Double, and partner Double Battles.
- Permanent stat calculation, IVs, EVs, natures, temporary stat stages, types,
  move data, PP, accuracy, critical hits, final damage, and reusable effects.
- Major status, the approved volatile set, regular weather, all four terrains,
  standard hazards, screens, rooms, and approved side conditions.
- The approved Ability, held-item, battle-item, and canonical move proof sets.
- Parties, targeting, switching, replacements, Shift/Set, battle outcomes,
  capture, escape, reinforcement, and partner ownership rules.
- Immutable snapshots, typed decisions, deterministic ordered events, and
  filtered selector observations for future UI, AI, and progression systems.
- Unreal Data Table adapters that copy validated rows into immutable plain-C++
  definitions.

This roadmap does not promise a bespoke implementation of every canonical
move, Ability, item, or content-specific exception.

## Governing Scope Rule

The reusable full-core roadmap governs battle-core architecture. Implementation
remains package-limited: each session implements only its current package and
must not add later-package behavior or speculative public interfaces. The
first-battle GDD continues to bound presentation and shipped content; this
roadmap does not authorize unrelated game systems.

## Scope Boundary

Included:

- Pure permanent-stat calculation from base stats, level, IV, EV, and nature.
- Transient battle state and deterministic rule resolution.
- Read-only inputs and output events needed by later systems.
- A selector interface and deterministic test selector, but not strategic AI.
- Capture and Bag consumption results, but not persistent inventory or storage
  writes.

Excluded:

- EXP formulas, EXP distribution, EV awards, level growth, move learning, and
  evolution.
- Money, rematches, defeat penalties, Pokédex progress, quests, and rewards.
- Save/load, autosave, permanent party/storage mutation, transport, healing,
  and all other overworld consequences.
- UMG, Battle Info layout, input, animation, camera, VFX, audio, localization,
  accessibility presentation, and assets unrelated to Data Tables.
- Mega Evolution, Dynamax, Terastallization, Triples, quadruples, Nuzlocke, and
  Champion-specific rules.

## Authority and Evidence

Use this precedence for every rule:

1. Explicit Solarus rule in `docs/battle-system-interview-handoff.md`.
2. The rules snapshot produced by B00B from the pinned modern references.
3. Ask the user if the pinned evidence conflicts or remains ambiguous.

Do not silently choose between conflicting sources. Do not describe the pinned
community engines as official sources.

Pinned engineering references:

- Pokemon Showdown commit `34caa98811fd6ed5d2f173ec1fc29dd9bd4bc91d`.
- Smogon damage-calc commit `83807801012f0af3e2dbb543d6fd40b483b3ebab`.
- Scarlet 1.1.0 capture disassembly revision
  `31d6aa136883ab354f5e8151526ee40f07317be0`.
- Epic Unreal Engine 5.8 Data Table documentation.

The rules-specific supplementary pins are recorded under B00B accepted evidence
and in the accepted snapshot. They do not widen the general authority order.

## Package Map

| ID | File | Priority | Hard dependencies | Session shape |
|---|---|---:|---|---|
| B00 | `01-live-baseline-and-rules-snapshot.md` | P0 | None | A then B |
| C01 | `02-core-contracts-events-and-rng.md` | P0 | B00 | A then B |
| C02 | `03-stats-types-moves-and-data-adapters.md` | P0 | C01 | A and B logically parallel |
| C03 | `04-battle-state-snapshots-and-decisions.md` | P0 | C02A, C02B | A then B |
| C04 | `05-actions-order-and-targeting.md` | P1 | C03 | A then B |
| C05 | `06-hit-damage-effects-and-outcomes.md` | P1 | C03; C04B before integration | A, B, then C |
| C06 | `07-parties-switching-and-replacements.md` | P1 | C05C | A then B |
| C07 | `08-status-volatiles-field-and-side-conditions.md` | P1/P2 | C06B | A, B/C, then D |
| C08 | `09-abilities-held-items-and-battle-items.md` | P2 | C07D | A, then B/C |
| C09 | `10-encounters-capture-escape-and-partner.md` | P2/P3 | C06B, C08B, C08C | A, B, then C |
| C10 | `11-canonical-proof-content.md` | P3 | C07D, C08, C09 | A then B |
| C11 | `12-integration-and-release-gate.md` | Completion gate | All applicable packages | A then B |

Priority meaning:

- P0: freezes contracts or facts needed by every later package.
- P1: builds the correct playable battle loop.
- P2: adds cross-system rules and modifiers.
- P3: adds special encounter flows and canonical proof content.
- Completion gate: must pass before calling the reusable battle core complete.

## Dependency Graph

```text
B00A Live baseline -> B00B Rules snapshot
  -> C01A IDs/RNG -> C01B Events/public contract
      -> C02A Stats/stages ---------+
      -> C02B Types/moves/adapters -+
                                      -> C03A State -> C03B Decisions/snapshots
                                          -> C04A Legality/order -> C04B Targeting
                                          -> C05A Hit/damage -----+
                                                                     -> C05B Effects
                                                                     -> C05C Faint/outcomes
                                                                         -> C06A Switching
                                                                         -> C06B Replacements
                                                                             -> C07A Trigger framework
                                                                                 -> C07B Major status
                                                                                 -> C07C Volatiles
                                                                                     -> C07D Field/side rules
                                                                                         -> C08A Hooks
                                                                                             -> C08B Abilities
                                                                                             -> C08C Items
                                                                                                 -> C09A Policies
                                                                                                 -> C09B Wild flows
                                                                                                 -> C09C Partner
                                                                                                     -> C10 Content
                                                                                                         -> C11 Gate
```

## Session and Parallel-Work Rules

Sequential execution is the default. The current workspace has Git metadata and
one initial commit, but no approved isolated branches, worktrees, or copies for
concurrent writing. Every agent shares the same source, `Binaries`,
`Intermediate`, `Saved`, and module outputs.

Logical parallel lanes after their contracts freeze:

- C02A permanent stats versus C02B nature/type/move/adapters. C01 freezes the
  small resolved nature-modifier value; C02B alone owns nature IDs and rows.
- C04A action legality/order versus the pure C05A hit/damage calculator work.
- C07B major-status content versus C07C volatile content.
- Weather content versus terrain/side-condition content after C07A.
- C08B Ability content versus C08C item content.
- Disjoint canonical Data Table families in C10.

Actual concurrent writing is allowed only after the user creates a trusted Git
baseline and supplies isolated branches, worktrees, or copies. Each lane must
own disjoint files. Unreal builds and headless tests remain serialized. Shared
headers, `FBattleEngine`, battle state, action resolution, and integration tests
have one owner at a time.

Agents must not initialize Git, commit, push, merge, reset, or delete files
without explicit user authorization.

## Required Session Completion Contract

Every implementation session must:

1. Read this index, its package file, the live source, and B00B's rules snapshot.
2. Record exact owned files and files it must not edit.
3. Record pre-run hashes for relevant source/tests, `.uproject`, and
   `DefaultEngine.ini`.
4. Add focused deterministic tests under `PokemonSolarus.Battle.<Subsystem>`.
5. Run the focused tests, build `PokemonSolarusEditor Win64 Development`, and
   run the full `PokemonSolarus.Battle` suite.
6. Export unique timestamped logs and JSON reports; do not overwrite historical
   evidence.
7. Report failures, warnings, not-run tests, and unrelated engine-startup noise
   separately.
8. Compare configuration hashes after Unreal. Never remove or rewrite the
   existing Android File Server block without approval.
9. Update only its package status and handoff. Do not begin the next package in
   the same session unless the roadmap explicitly groups it.

## Fixed Solarus Decisions Added During Planning

- Field proof set: four regular weather states, four terrains, four hazards,
  three screens, three rooms, Tailwind, Safeguard, and Mist.
- Volatile proof set: confusion, flinch, Protect, Leech Seed, partial trapping,
  switch-prevention trapping, Taunt, Encore, Disable, Substitute, charging,
  recharge, and semi-invulnerability.
- Ability proof set: Blaze, Overgrow, Intimidate, Levitate, Drizzle, Speed
  Boost, Magic Guard, and Mold Breaker.
- Held-item proof set: Leftovers, Sitrus Berry, Lum Berry, Focus Sash, Life Orb,
  Choice Band, Heavy-Duty Boots, Air Balloon, and Quick Claw.
- Battle-item proof set: Poke Ball, Hyper Potion, Revive, Full Heal, and X
  Attack.
- Proof mechanics receive canonical named rows, not only anonymous test data.
- Escape attempt counter `C` starts at one, increments after each legal failed
  attempt, and never resets during that battle.
- Casual eligible Single Trainer Battles default to Shift; unsupported formats
  always use Set.
- Player-controlled actions are requested first; partner AI may observe those
  selections, while enemy selectors receive a filtered pre-choice view.
