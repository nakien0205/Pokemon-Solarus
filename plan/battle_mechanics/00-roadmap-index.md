# Global Battle Mechanics Roadmap

Status date: 2026-08-29
Roadmap status: Approved and materialized; B00 through C09 package delivery,
the ADR-0002 closeout, and the behavior-preserving Battle structural splits are
complete under focused validation; C10 and C11 remain
Current implementation state: ADR-0002 passed at
`b5db3e440d7c6eb5ba6ddbcc01a92a3c9b8756c0`, and the required
behavior-preservation validation is complete. A non-runtime executor
helper-declaration follow-up is documented in the structural guides
Next remediation lane: R5 C10A Held-item move intents

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
- C06A's canonical party-reserve resolver, typed switch blockers and transfer
  policy, execution-time voluntary-switch revalidation, deterministic forced
  switching, post-move pivot request, stable active-slot occupancy change,
  transient cleanup and entry-trigger facts, and seven
  `PokemonSolarus.Battle.C06A.*` Automation tests.
- C06B's frozen queue-boundary replacement groups, actorless mandatory
  replacement decisions, atomic Left/Right reserve assignment, free Shift
  accept/decline path, unsupported-format Set normalization, deterministic
  entry facts and replay, and eight `PokemonSolarus.Battle.C06B.*` Automation
  tests.
- C07A's standalone trigger scheduler, 17 frozen phases, typed
  Condition/Ability/item sources and battle subjects, deterministic
  caller-directed ordering with no RNG tie draw, deferred declarative effect
  requests, reentrancy guards, duration/expiry/layer/suppression state, typed
  cleanup, fact-only lifecycle output, and seven
  `PokemonSolarus.Battle.C07A.*` Automation tests.
- C07B's six canonical major statuses, deterministic application/action/
  residual rules, trigger integration, private Sleep/Toxic state, ordered
  faint/replacement continuation, and nine `PokemonSolarus.Battle.C07B.*`
  Automation tests.
- C07C's 13 approved canonical volatiles, shared trigger registrations,
  selection/action/hit/damage/end-turn integration, deterministic charge and
  fainted-target fallback, private last-move/payload state, and eight
  `PokemonSolarus.Battle.C07C.*` Automation tests.
- C07D's 21 approved weather, terrain, hazard, screen, room, and side
  conditions; shared trigger lifecycles; live damage, order, status, item-
  suppression, switch-in, faint-boundary, duration, expiry, and snapshot
  integration; and nine `PokemonSolarus.Battle.C07D.*` Automation tests.
- C08A's semantic Ability/item hook vocabulary, validated C07A request bridge,
  no-leak reveal tracker, typed atomic held-item ownership ledger and final
  facts, separate finite Trainer Bag snapshots and per-Trainer turn quotas, and
  seven `PokemonSolarus.Battle.C08A.*` Automation tests.
- C08B's eight approved concrete Abilities, reusable C08A/C07A hook
  registrations, deterministic effective-Speed entry order, live move,
  grounding, hazard, terrain, residual, switch, faint, suppression, and public
  reveal integration, and twenty `PokemonSolarus.Battle.C08B.*` Automation
  tests.
- C08C's nine approved held items and five approved Bag items, reusable
  C08A/C07A hooks, live damage/recovery/status/hazard/order/switch/faint and
  reveal integration, exact Bag item/target pairings, stale-action
  revalidation, separate Trainer counts/quotas, frozen Poke Ball handoff, and
  twenty-six `PokemonSolarus.Battle.C08C.*` Automation tests.
- C09A's deterministic setup-to-policy compiler for all five encounter kinds
  and three supported formats, per-Trainer Bag/Revive/Run/capture and selector
  routing, wild-only command/flee constraints, multi-active wild reinforcement,
  scripted ending and partner-ownership policy, filtered immutable selector
  input, legal-only selector boundary, test-only scripted selector, and six
  `PokemonSolarus.Battle.C09A.*` Automation tests.
- C09B session 1's exact Scarlet/Violet capture calculation and early-stopping
  RNG, capture selection/execution, pending Party/Storage destinations, retained
  capture facts, public event/result metadata, replay schema 5, complete shared
  C09B setup/snapshot/replay fields, and four
  `PokemonSolarus.Battle.C09B.Capture.*` Automation tests.
- C09B session 2's exact permanent-Speed Run formula and counter, strict RNG
  boundaries, default-disabled configured WildFlee with typed trigger and
  eligibility identities, per-actor removal and `Escape/OpponentFled` outcome,
  replay equality, and three focused Run/WildFlee Automation tests. The final
  C09B filter passes all seven Capture/Run/WildFlee tests.
- C09C's frozen separate player/partner ownership and resolved control,
  partner-visible player commands, legal allied support, owner-scoped Bags and
  switches, capture and exhausted-slot restrictions, player-wipe continuation,
  typed Partner Team Victory recovery, core-only persistent EXP/EV
  ineligibility facts, replay schema 6, and six
  `PokemonSolarus.Battle.C09C.*` Automation tests.
- Cry for Help and wild reinforcement are **Freeze until call by user**. Their
  existing code, setup, state, snapshot, replay, policy, and test scaffolding
  remains unchanged. They are not part of C09 acceptance and do not block C10.

There is now one authoritative internal battle-state owner and a deterministic
normal-turn selection, queue-lock, action-start, final-target, switching,
trigger scheduling, major-status, approved volatile, and field/side-condition
seams, plus concrete Ability and item execution and the frozen public setup/
decision/event/snapshot/replay, stat, type, move, definition, adapter, and pure
hit/damage language needed by later packages, together with C09A's encounter
policy and selector seams. The separately tracked production runtime/HUD slice
is accepted by the user. C09B Capture, Run, and configured WildFlee now exist;
C09C now supplies the PartnerDouble ownership, continuation, outcome-recovery,
progression-fact, and replay flow. Cry for Help remains frozen.
The completed Story 001
and its 33-test report
describe an older source state and
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
- B00 through C09 package delivery is complete. The ADR-0002 implementation
  closeout passed, and the later behavior-preserving Battle Engine and executor
  structural splits were validated and published. C10A remediation R1-R4B is
  complete; R5 is next. C10A row authoring is not started and remains blocked
  through R5, R6, and independent R7. C11 remains blocked behind C10.
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
- C05 is complete under the approved focused-validation scope. C06A was not
  implemented by C05C; its later completion is recorded immediately below.

## C06A Execution Status

- C06A completed on 2026-08-21 from the clean `c1d478d` baseline. The live
  roadmap cleared its dependency before writing; no C06B implementation was
  included.
- The new reusable resolver returns party-slot-ordered legal reserves, rejects
  empty/active/fainted/Egg/captured/removed/wrong-owner/reserved candidates,
  accepts typed encounter/trapping facts, keeps Baton-style transfer typed but
  blocked, and performs exactly one `U[0,n-1]` draw for a non-empty forced
  switch list, including `U[0,0]`.
- `FBattleEngine::ExecuteCurrentSwitch()` revalidates before mutation, preserves
  the structural active-slot ID and persistent Pokemon facts, clears stages and
  ordinary volatiles, and executes distinct allied choices through the locked
  queue. Ordinary wild-opponent switching is unavailable while player,
  partner-role, and Trainer-party rules remain permitted by the typed policy.
- Forced effects resolve without a request. A reached pivot effect publishes
  one `EBattleDecisionRequestKind::PivotSwitch` request only when its source is
  still active and a reserve exists. Invalid or stale responses retain the
  request and consume no RNG; a valid response switches before
  `ActionCompleted` without another action cost.
- The public contract appends `SwitchTransientStateCleared` and reuses
  `Switched` as the entry-trigger fact. The generic decision/event replay
  encoding required no wire-shape or schema-version change.
- The exact final build command was:
  `Build.bat PokemonSolarusEditor Win64 Development "D:\Python\Projects\Pokemon Solarus\Game\PokemonSolarus.uproject" -WaitMutex -ForceUnity -DisableAdaptiveUnity -NoUBA`.
  It succeeded with exit code `0`; evidence is
  `Game/Saved/Automation/C06A-Switching-Final2-20260821T101747Z/build.log`.
- The exact runtime filter was
  `Automation RunTests PokemonSolarus.Battle.C06A`. It discovered exactly seven
  tests: 7 succeeded, 0 succeeded with warnings, 0 failed, 0 not run, and 0 in
  process. Every test entry records 0 warnings and 0 errors, and the process
  exit code was `0`:
  `Game/Saved/Automation/C06A-Switching-Final2-20260821T101747Z/report/index.json`.
  The matching log is
  `Game/Saved/Automation/C06A-Switching-Final2-20260821T101747Z/automation.log`.
- No C05C, older package, complete battle, or full project filter was run. No
  fresh runtime claim is made for those filters.
- Final hashes for the three new C06A files are `0ae109bab80368304d24af39c23ddedb9d3eb70430ca340f210a65c1a6d917ce`
  (`BattleSwitching.h`), `556b6335e620221f91b66307385355291695d6bcd4fe00821ec9e9a653a36d9b`
  (`BattleSwitching.cpp`), and `af648aa140796af1191a2cad0db84a463ee88b38aec3a271bda32c7c404673e0`
  (`BattleSwitchingTests.cpp`). The full owned/protected SHA-256 ledger and
  exact commands are in `07-parties-switching-and-replacements.md`.
- `CLAUDE.md`, B00B, the Solarus interview handoff, `.uproject`,
  `DefaultEngine.ini`, module rules, and every existing test file remained at
  their recorded hashes. No `dev-story`, subagent, Git commit, or other Git
  write was used.
- C06A is complete under the approved focused-validation scope. C06B's later
  completion is recorded immediately below.

## C06B Execution Status

- C06B completed on 2026-08-21 from clean `main` baseline `7330dda`
  (`Implement C06A`). C07A was not started.
- Setup now defaults eligible non-Wild Single encounters to Shift, preserves an
  explicit forced-Set choice, and normalizes Wild, Double, and Partner Double
  setups to Set.
- Queue exhaustion freezes replacement needs in side/Left/Right order. The
  engine emits each existing `ReplacementRequired` fact once, offers an
  eligible player Shift response first, then exposes actorless owner-scoped
  mandatory replacement batches. One reserve falls back to Left when both
  slots are empty.
- Replacement and Shift choices are revalidated before mutation. Rejected
  stale, wrong-owner, duplicate-reserve, or illegal responses retain gameplay
  version, occupancy, pending requests, and RNG trace. Accepted replacement is
  actionless; accepted Shift reuses C06A transient cleanup; decline emits only
  `DecisionAccepted` before opponent replacement.
- No phase, event enum, replay field/schema version, public engine method,
  hazard execution, Ability execution, or item execution was added.
- The final forced-unity `PokemonSolarusEditor Win64 Development` build with
  `-ForceUnity -DisableAdaptiveUnity -NoUBA` succeeded with exit code `0`.
- The only runtime filter run was
  `Automation RunTests PokemonSolarus.Battle.C06B`, under run ID
  `C06B-Replacements-20260821T111345Z`. Its `index.json` records exactly 8
  succeeded, 0 succeeded with warnings, 0 failed, 0 not run, and 0 in process;
  every test entry records 0 warnings and 0 errors, and the process exited `0`.
- The editor log contains one pre-test command-line deprecation warning that
  `-ReportOutputPath` is now named `-ReportExportPath`; it contains no C06B test
  warning or failure. Report evidence is
  `Game/Saved/Automation/C06B-Replacements-20260821T111345Z/report/index.json`.
- No C06A, C05C, older battle, complete battle, or full project test filter was
  run. No fresh runtime claim is made for those filters.
- Protected authorities, C05C, event/replay files, module/configuration files,
  and existing tests retained their recorded pre-write hashes. No `dev-story`,
  subagent, Git commit, or other Git write was used.
- C06 is complete under its approved focused-validation scope. C06B cleared
  C07A's dependency; C07A's later completion is recorded immediately below.

## C07A Execution Status

- C07A completed on 2026-08-21 from clean `main` baseline
  `d3d8addce481a9a2e682a0782e5fe226f2b4a6c1`.
- The new standalone `FBattleTriggerFramework` schedules declarative requests
  only. It freezes all 17 phases, deep-copies and validates registrations,
  orders canonical keys under explicit directions without RNG, defers queued
  phases, guards non-repeatable triggers per token, decrements finite duration
  before effects, queues expiry, preserves layer/suppression state, applies
  typed cleanup, and emits ordered fact-only lifecycle records.
- No `FBattleEngine` integration, concrete C07B/C07C/C07D behavior, Data Table
  expansion, callback execution, event/replay/snapshot change, or existing
  runtime/test modification was included.
- The required forced-unity `PokemonSolarusEditor Win64 Development` build with
  adaptive unity disabled succeeded with exit code `0`. Evidence is
  `Game/Saved/Automation/C07A-TriggerFramework-20260821T135944Z/build.log`.
- The only runtime filter run was
  `Automation RunTests PokemonSolarus.Battle.C07A`. Its exported `index.json`
  records exactly 7 succeeded, 0 succeeded with warnings, 0 failed, 0 not run,
  and 0 in process; every path starts with the C07A filter, every entry has 0
  warnings and 0 errors, and the process exited `0`. The report and log are
  under `Game/Saved/Automation/C07A-TriggerFramework-20260821T135944Z/`.
- The user's focused-test instruction superseded the roadmap's generic
  full-suite requirement. No C06, older battle, complete battle, or full
  project filter was run, and no fresh runtime claim is made for them.
- Protected pre-existing battle sources/tests, the three authorities,
  `.uproject`, `DefaultEngine.ini`, module rules, and `FoundationMap.umap`
  retained their pre-write SHA-256 hashes. No `dev-story`, subagent, Git
  commit, or other Git write was used.
- C07A is complete under the approved focused-validation scope. C07B and C07C
  are next and were not started; C07D remains later.

## C07B Execution Status

- C07B completed on 2026-08-22 from clean `main` baseline
  `5d1085b7cf2f6797e2d70919ce2f5fd766cc61fd`.
- `BattleMajorStatus` now defines the six canonical major statuses, their typed
  application/action/residual results, exact formulas, RNG purposes, neutral
  future prevention hooks, and C07A trigger registrations. Only Burn,
  Paralysis, Sleep, Freeze, Poison, and Toxic receive C07B behavior; arbitrary
  existing `MajorStatus` IDs keep generic storage behavior.
- The live engine enforces mutual exclusion and type immunity before status
  RNG; runs Sleep, Freeze, and Paralysis gates before PP; applies Paralysis
  after Speed stages and Burn through final-damage input; thaws Freeze at the
  reached-target point; and resolves ordered residual mutations, fainting,
  replacements, terminal outcomes, and next-turn decisions.
- Sleep duration and Toxic stage remain private C07A runtime facts. Existing
  event types and replay schema `4` are unchanged, and observer snapshots still
  expose only the public major-status ID.
- The required `PokemonSolarusEditor Win64 Development` build succeeded with
  `-ForceUnity -DisableAdaptiveUnity -NoUBA` and exit code `0`.
- Only `Automation RunTests PokemonSolarus.Battle.C07B` was run for final
  acceptance. The unique exported evidence at
  `Game/Saved/Automation/C07B-20260822-092726/index.json` records exactly nine
  successes and zero warnings, failures, not-run, or in-process entries; every
  test entry has zero warnings and errors.
- Protected authorities, existing test files, module rules, `.uproject`,
  configuration, assets, and Git history were not modified. No `dev-story`,
  subagent writing, commit, older battle filter, full battle suite, or
  project-wide test run was used.
- C07B is complete under the approved focused-validation scope. C07C is next
  and was not started; C07D remains later.

## C07C Execution Status

- C07C completed on 2026-08-22 from clean `main` baseline
  `0ca58edd5a180e08501adb13b2f84e35db204eb7`.
- `BattleVolatile` defines exactly the 13 approved canonical volatiles, their
  typed application/action/residual rules, deterministic RNG purposes,
  duration/layer contracts, and C07A trigger registrations.
- The live engine integrates volatile selection legality, action gates,
  Protect and Fly reachability, Substitute damage routing, trapping switch
  blocks, charge/recharge locks, fainted-target fallback, ordered Leech Seed
  and bind residuals, and typed switch/faint/battle-end cleanup. Successful
  Protect chains survive protection-breaking, while denied or aborted charged
  releases clear Charging and Fly state without a second PP cost.
- Three explicit move flags cover Fly reach/double-power and protection
  breaking. Catalog and runtime validation reject malformed charge effect
  ordering. Private last-move and charge payload facts support Encore,
  Disable, and forced releases without changing snapshot, event, or replay wire
  contracts.
- The required `PokemonSolarusEditor Win64 Development` build succeeded with
  `-ForceUnity -DisableAdaptiveUnity -NoUBA` and exit code `0`.
- Only `Automation RunTests PokemonSolarus.Battle.C07C` was run for final
  acceptance. The exported report at
  `Game/Saved/Automation/C07C-final-20260822-114857/index.json` records exactly
  8 succeeded, 0 succeeded with warnings, 0 failed, 0 not run, and 0 in
  process; every test path uses the C07C prefix and every entry has 0 warnings
  and 0 errors.
- C07D behavior, assets, UI, configuration, module rules, existing tests, the
  B00B snapshot, the Solarus interview handoff, and Git history were not
  modified. No `dev-story`, commit, older battle filter, full battle suite, or
  project-wide test run was used.
- C07C is complete under the approved focused-validation scope. C07D is
  dependency-clear and next; later packages remain blocked or not started.

## C07D Execution Status

- C07D completed on 2026-08-22 from `main` baseline
  `011acf8d20aea3d85119dee9a46e6dc592f6c057`, while preserving the
  pre-existing `FoundationMap.umap` modification.
- `BattleFieldSideConditions` defines exactly the 21 approved conditions and
  registers their shared source, owner, duration, layer, expiry, and cleanup
  lifecycles through C07A.
- The live engine now applies weather/terrain damage and residual rules,
  grounded checks, screens, rooms, Tailwind, Safeguard, Mist, public duration
  and layer snapshots, and deterministic condition event sourcing. Entry
  hazards execute by creation order on voluntary, forced, Pivot, Shift, and
  mandatory replacement entry, with a faint boundary after every HP mutation.
- Damage, order, status, item, and switch-in hazard behavior is admitted through
  emitted C07A trigger requests, so suppressed registrations do not act from
  stored condition state alone.
- Two generic move flags expose side-protection bypass and Grassy Terrain move
  reduction. Neutral Ability/item prevention, grounding, duration-extension,
  and priority hooks remain available for C08 without implementing C08 content.
- The final forced-unity `PokemonSolarusEditor Win64 Development` build
  succeeded with `-ForceUnity -DisableAdaptiveUnity -NoUBA` and exit code `0`:
  `Game/Saved/Automation/C07D-final-20260822-060928Z/build.log`.
- Only `Automation RunTests PokemonSolarus.Battle.C07D` was run for final
  acceptance. The report at
  `Game/Saved/Automation/C07D-final-20260822-060928Z/report/index.json`
  records exactly 9 succeeded, 0 failed, 0 not run, and 0 in process; every
  entry has 0 warnings and 0 errors, and the process exited `0`.
- Excluded field content, concrete Ability/item behavior, assets, UI,
  configuration, module rules, existing tests, the B00B snapshot, the Solarus
  interview handoff, and Git history were not modified. No `dev-story`, commit,
  older battle filter, full battle suite, or project-wide test run was used.
- C07 is complete under the approved focused-validation scope. C08A is
  dependency-clear and next; later packages remain blocked or not started.

## C08A Execution Status

- C08A completed on 2026-08-22 from `main` baseline
  `7f6503598849cb2b5efb4a33b097b9950a4fde4a`, while preserving the
  pre-existing `FoundationMap.umap` modification.
- `BattleAbilityItemContracts` defines the complete shared semantic hook-point
  and typed effect-request vocabulary, validates registrations through C07A,
  and converts matching deterministic trigger requests without executing
  concrete Ability or item content.
- Public-safe activation facts follow explicit reveal policy. Hidden
  ineligible, suppressed, ignored, and non-public prevented activations emit no
  fact; first and repeat public reveals are stable per source definition and
  owner.
- Stable held-item instances keep original owner/item separate from current
  transient state. Typed atomic suppression, reveal, consume, restore, remove,
  swap, and temporary-steal operations feed battle-end facts that preserve
  consumption or restoration, reset temporary ownership, retain captured
  original-owner items, and remove battle-generated items without persistence.
- Separate finite Trainer Bag snapshots enforce one action per Trainer per
  turn. Pre-use rejection consumes nothing; a legal use consumes the item and
  action even when the later effect is prevented.
- The final forced-unity `PokemonSolarusEditor Win64 Development` build
  succeeded with `-ForceUnity -DisableAdaptiveUnity -NoUBA` and exit code `0`:
  `Game/Saved/Automation/C08A-Contracts-20260822T075240Z/build.log`.
- Only `Automation RunTests PokemonSolarus.Battle.C08A` was run. The exported
  report at
  `Game/Saved/Automation/C08A-Contracts-20260822T075240Z/report/index.json`
  records exactly 7 succeeded, 0 failed, 0 not run, and 0 in process; every
  test has 0 warnings and 0 errors, and the process exited `0`.
- Concrete C08B Abilities, concrete C08C held/Bag items, `FBattleEngine`
  integration, event/replay/snapshot changes, persistent inventory writes,
  catalogs/Data Tables, assets, UI, configuration, module rules, existing
  tests, the B00B snapshot, and the Solarus interview handoff were not modified.
  No `dev-story`, commit, older battle filter, full battle suite, or
  project-wide test run was used.
- C08A is complete under the approved focused-validation scope. C08B and C08C
  are dependency-clear; C09 and later packages remain blocked or not started.

## C08B Execution Status

- C08B completed on 2026-08-22 from `main` baseline
  `7f6503598849cb2b5efb4a33b097b9950a4fde4a`, with the completed C08A
  worktree and pre-existing `FoundationMap.umap` modification preserved.
- `BattleAbility` defines exactly the eight approved proof Abilities and maps
  each one to reusable C08A/C07A hook definitions. The rules cover low-HP Q12
  offense, entry stat/field effects, grounding immunity, end-turn Speed,
  indirect-damage prevention, eligible defensive bypass, suppression, reveal,
  and deterministic ordering without species branches.
- The engine registers hooks for starting and incoming actives, resolves entry
  Abilities by effective Speed, uses the same effective-Speed helper as action
  ordering, integrates move/hazard/terrain/residual behavior, and cleans hooks
  on switch and faint boundaries. `AbilityActivated` was appended without
  renumbering existing events or changing the replay schema.
- The final all-source `PokemonSolarusEditor Win64 Development` build succeeded
  with `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA` and exit
  code `0`:
  `Game/Saved/Automation/C08B-Abilities-Final-20260822T100431Z/build-single-source-unity.log`.
  A packed-unity attempt without the one-source split exposed only pre-existing
  ambiguous helper names between untouched `BattleActionQueueTests.cpp` and
  `BattleFieldSideConditionTests.cpp`; those older tests were kept outside the
  C08B write set.
- Only `Automation RunTests PokemonSolarus.Battle.C08B` filters were run. The
  final exported report at
  `Game/Saved/Automation/C08B-Abilities-Final-20260822T100431Z/report-final/index.json`
  records exactly 20 succeeded, 0 succeeded with warnings, 0 failed, 0 not run,
  and 0 in process; all 20 paths use the C08B prefix, every entry has 0 warnings
  and 0 errors, and the process exited `0`. The matching command log is
  `Game/Saved/Automation/C08B-Abilities-Final-20260822T100431Z/automation-final.log`.
- C08A source/tests, concrete C08C items, catalogs/Data Tables, assets, UI,
  configuration, module rules, the `.uproject`, existing tests, the B00B
  snapshot, and the Solarus interview handoff were not modified. No
  `dev-story`, commit, older battle filter, full battle suite, or project-wide
  test run was used.
- C08B is complete under the approved focused-validation scope. C08C is
  dependency-clear; C09 still requires C08C.

## C08C Execution Status

- C08C completed on 2026-08-23 from `main` baseline
  `61b2d8f16e1dfbf245e84baa11a2cc20177ac861`, while preserving the unrelated
  dirty map and concurrent UI-asset work.
- `BattleItem` defines the nine approved held-item rules and reusable C08A/C07A
  hooks. The live executor and engine integrate recovery, cure, per-hit faint
  prevention, damage/recoil, Choice lock with generic no-leak cancellation,
  hazard/Ground interactions, Air
  Balloon reveal/pop/known-empty projection, Quick Claw RNG/order, suppression,
  switch/faint cleanup, and deterministic reveal ordering without species
  branches.
- The held-item ledger proof covers consumption/Recycle, Knock Off-style
  removal, Trick-style swapping, Thief-style temporary theft, capture, and
  battle-generated cleanup. It emits sufficient final ownership facts without
  writing persistent inventory.
- `BattleBagItem` defines exactly Poke Ball, Hyper Potion, Revive, Full Heal,
  and X Attack. Exact item/target pairs and execution-time revalidation enforce
  owner targeting, separate finite Trainer Bags, one Bag quota per Trainer per
  turn, and no item/quota/RNG consumption for pre-use or stale rejection.
- Hyper Potion heals 120 HP capped at Max HP; Revive restores
  `max(1, floor(MaxHP / 2))`; Full Heal clears all six canonical major statuses,
  Confusion, and Toxic's private counter while retaining unrelated volatiles;
  X Attack raises the acting battler's Attack by two stages capped at `+6`.
- Under approved Option A, C08C rejects Revive for every opponent Trainer and
  defers explicit boss permission to C09A. Partner capture is rejected. Poke
  Ball remains a true no-op handoff for C09, without C08C capture math,
  capacity, removal, completion, consumption, RNG, history, or counter changes.
- The final all-source `PokemonSolarusEditor Win64 Development` build succeeded
  with `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA` and exit
  code `0`:
  `Game/Saved/Automation/C08C-Items-20260823T051131Z/build-final.log`.
- Only `Automation RunTests PokemonSolarus.Battle.C08C` was run. The final
  exported report at
  `Game/Saved/Automation/C08C-Items-20260823T051131Z/report-final/index.json`
  records exactly 26 succeeded, 0 succeeded with warnings, 0 failed, 0 not run,
  and 0 in process. All 26 paths use the C08C prefix, every entry has 0 warnings
  and 0 errors, and the process exited `0`. The report SHA-256 is
  `053D8B23A2D2612FA7FEA280042F68EDBEE84697795E86684D7AB4AB816DEF09`; the
  matching command log is
  `Game/Saved/Automation/C08C-Items-20260823T051131Z/automation-final.log`.
- The C08C implementation did not modify C09 flows, persistent inventory
  writes, catalogs/Data Tables, assets, UI, configuration, module rules, the
  `.uproject`, non-C08C tests, the B00B snapshot, or the Solarus interview
  handoff. No
  `dev-story`, commit, older battle filter, full battle suite, or project-wide
  test run was used.
- At the C08C checkpoint, C08C and C08 were complete, C09A was
  dependency-clear, and C09B and later packages remained dependency-blocked.

## C09A Execution Status

- C09A completed on 2026-08-24 from clean `main` baseline
  `2dbcc0122f027f53927744102f8d3503e8db238e`. The user explicitly verified and
  accepted the production runtime/HUD slice, clearing the prior manual gate.
- `BattleEncounterPolicy` compiles the five encounter kinds across Single,
  Double, and PartnerDouble into immutable-by-interface typed policies. It
  freezes active/party limits, Run, capture, Bag, Revive, Shift/Set,
  reinforcement, configured wild fleeing, scripted ending, partner ownership,
  and Wild/Basic/Skilled/Boss/Tutorial/Partner selector routing.
- Wild encounters alone may expose Run/capture/flee policy. Wild opponents are
  rejected if authored with a Trainer Bag, Wild Single does not claim an
  unavailable right-slot reinforcement, and only an explicitly stocked
  Boss/Gym Bag may generate an opponent Revive action.
- `IBattleActionSelector` receives a deep-copied observer-filtered snapshot and
  one core-generated request. The boundary checks the chosen typed decision
  through the request's existing `Allows` path; final engine submission retains
  stale-state revalidation. The deterministic FIFO selector is test-only.
- The final `PokemonSolarusEditor Win64 Development` build succeeded with
  `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA` and exit code
  `0`:
  `Game/Saved/Automation/C09A-PoliciesSelectors-Final-20260824T095839Z/build.log`.
- Only `Automation RunTests PokemonSolarus.Battle.C09A` was run. The final
  exported report at
  `Game/Saved/Automation/C09A-PoliciesSelectors-Final-20260824T095839Z/report-final/index.json`
  records exactly 6 succeeded, 0 succeeded with warnings, 0 failed, 0 not run,
  and 0 in process. All six paths use the C09A prefix, every entry has 0
  warnings and 0 errors, process exit code is `0`, and report SHA-256 is
  `c72f3b076d8f7fcec3af65ce3d4600a579d97221c7a2be18ad8f762e5fb3cd11`.
- An earlier focused rerun under
  `C09A-PoliciesSelectors-Final-20260824T095721Z` stopped on a C09A test-fixture
  assertion while constructing an invalid seventh party slot and exported no
  report. The fixture was corrected before the final build and run above; that
  interrupted run is not acceptance evidence.
- At C09A completion, C09B/C09C execution, strategic AI/scoring, team authorship/tuning, replay
  schema changes, persistent writes, assets, UI, configuration, module rules,
  the `.uproject`, non-C09A tests, the B00B snapshot, and the Solarus interview
  handoff were not modified. No `dev-story`, commit, older battle filter, full
  battle suite, or project-wide test run was used.
- C09A remains complete under the approved focused-validation scope. C09B's
  two completion records are below; C09C is now dependency-clear.

## C09B Session 1 Execution Status

- C09B session 1 completed on 2026-08-24 from baseline
  `2dbcc0122f027f53927744102f8d3503e8db238e`, while preserving the accepted
  uncommitted C09A work and every unrelated dirty change.
- `BattleCapture` implements the B00B Scarlet/Violet capture indicator,
  caught-count HP component, badge and status modifiers, critical capture and
  Catching Charm, low-level modifier, capture coefficient, single-precision
  `powf` shake threshold, exact `<` boundaries, guaranteed and must-capture
  paths, and early RNG stopping.
- Poke Ball selection and execution validate encounter/Trainer/target legality,
  finite item count, frozen party-plus-storage capacity including pending
  captures, dedicated immutable capture progression, species classification,
  and the catalog catch rate before consumption. Legal failures consume one
  item and action; stale/blocked attempts consume none.
- Successful capture retains HP, status, move PP, original/current held-item
  facts, removes only the exact target, and records ordered Party then Storage
  pending destinations. Captured actors and queued moves with that exact target
  cancel before target resolution or PP, without redirection. The last wild
  capture produces `Victory` with `Capture` cause.
- The complete shared C09B setup/snapshot/replay schema is frozen now: capture
  progression, capture capacity, species capture classification, configured
  reinforcement battler identity, one-based escape-attempt counter,
  reinforcement-success flag, pending-capture records, and public capture event
  metadata. Replay schema is `5`; session 2 requires no further schema-format
  change.
- The final `PokemonSolarusEditor Win64 Development` build succeeded with
  `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA`:
  `Game/Saved/Automation/C09B-Capture-20260824T125438Z/build-final.log`.
- Only `Automation RunTests PokemonSolarus.Battle.C09B` was used for final
  acceptance. The exported report at
  `Game/Saved/Automation/C09B-Capture-20260824T125438Z/report-final/index.json`
  records exactly 4 succeeded, 0 succeeded with warnings, 0 failed, 0 not run,
  and 0 in process. Every entry has 0 warnings and 0 errors; report SHA-256 is
  `9269891e673157050a0d5b4ad920760318b0e64f494b19c5697c4fe0bfd5f7ea`.
- The preceding `C09B-Capture-20260824T125253Z` audit rerun exposed a
  test-only pointer retained from a temporary snapshot. The helper was fixed
  before the final build/run above; that failed diagnostic run is not
  acceptance evidence.
- At session-1 completion, Run and configured WildFlee execution/tests, C09C,
  UI/assets, persistence writes, rewards, deployment, and Git writes had not
  been performed. No older Battle package filter, full Battle suite, or
  project-wide test suite was run.

## C09B Session 2 Execution Status

- C09B session 2 completed on 2026-08-24 from clean committed baseline
  `7965042a3730869fb5adc983f2551321186c7758`.
- `BattleWildFlow` owns the pure Run and WildFlee rules. Run uses permanent,
  unmodified Speed, the leftmost living wild opponent, the exact one-based
  formula/counter, strict `R < F`, and no draw when `F > 255`.
- Request generation rejects Trainer or Speed-below-four Run before action/RNG
  consumption. Other actions and switching do not reset the counter.
- Explicit WildFlee materializes one encounter-wide typed policy using
  `Trigger.C09B.WildFlee.ActionSelection` and
  `Eligibility.C09B.WildFlee.ActiveLivingWildOpponent`. Disabled generates no
  action; Never/Always consume no RNG; Chance draws exactly
  `U[0, denominator - 1]`.
- WildFlee uses the existing Run command band, spends no PP, removes only the
  fleeing actor, continues while another living wild opponent remains, and
  otherwise ends as `Escape/OpponentFled` without rewards or persistence.
- Cry for Help, wild reinforcement, and `CallReinforcement` are **Freeze until
  call by user**. All existing related code and tests were left unchanged.
- Replay schema remains `5`. No wire fields or enum ordinals changed.
- The `PokemonSolarusEditor Win64 Development` build succeeded with
  `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA`; the durable
  log is
  `Game/Saved/Automation/C09B-WildFlow-Final-20260824T135514Z/build-final.log`.
- Only `Automation RunTests PokemonSolarus.Battle.C09B` was run. The exported
  report at
  `Game/Saved/Automation/C09B-WildFlow-Final-20260824T135514Z/report-final/index.json`
  records 7 succeeded, 0 succeeded with warnings, 0 failed, 0 not run, and 0 in
  process. All paths use the C09B prefix and every entry has 0 warnings/errors.
- C09B is complete. Its historical completion boundary preceded C09C; the C09C
  completion record below supersedes this former next-package state.

## C09C Execution Status

- C09C completed on 2026-08-24 from clean committed baseline
  `8d52dfca58c879cf4d015a3f4cf35b0296232ee5` in one session without
  subagents because its engine, state, event, snapshot, replay, and focused-test
  edits share one integration boundary.
- PartnerDouble freezes distinct player and partner Trainers, parties, Bags,
  switches, action allowances, selectors, and resolved Human/PartnerAI control.
  The partner may observe the player's selected command while enemies may not,
  and allied-battler support targeting retains owner-party item restrictions.
- Partner capture and cross-owner item/switch targets are rejected. An
  exhausted partner slot stays empty, while a player-party wipe continues if
  the partner can still battle.
- `PartnerTeamVictory` restores the first valid player party entry to 1 HP,
  guarantees major-status removal, and emits the appended typed
  `PartnerTeamVictoryRecovery` event ordinal `52` before `BattleEnded`.
  Core-authority final snapshots also expose typed facts marking NPC partner
  Pokemon ineligible for persistent EXP and EV; core calculates and writes no
  progression.
- Canonical replay schema is `6`. Identical setup, decisions, and RNG reproduce
  the same event stream, final snapshot, and serialized replay.
- The final `PokemonSolarusEditor Win64 Development` build succeeded with
  `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA`; its log is
  `Game/Saved/Automation/C09C-Partner-Final-20260824T143500Z/build.log`.
- Only `Automation RunTests PokemonSolarus.Battle.C09C` was run. The final
  `Game/Saved/Automation/C09C-Partner-Final-20260824T143500Z/report/index.json`
  records exactly 6 succeeded, 0 succeeded with warnings, 0 failed, 0 not run,
  and 0 in process; all paths have the C09C prefix and 0 warnings/errors.
  Process exit code is `0`; report SHA-256 is
  `46eda7e469474a8078b43e5b2172aa0ee3bd3d1ba8c23d9de227c12eb8728fa7`.
- The first C09C-only diagnostic run passed 5/6 and exposed only test-fixture
  assumptions. A later completion audit added explicit starting-status cure
  proof; its first build exposed a test-only projection-type compile error.
  Both test issues were corrected, and the final replay proof excludes the
  test-only status mutation. Neither earlier diagnostic is acceptance evidence.
- Cry for Help/reinforcement, C10, UI/assets, persistent writes, reward
  calculation, configuration, module rules, `.uproject`, older package filters,
  the full Battle suite, and Git writes were not changed or run for C09C.

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
  capture, escape, configured wild fleeing, and partner ownership rules. Cry
  for Help and wild reinforcement remain **Freeze until call by user**.
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
