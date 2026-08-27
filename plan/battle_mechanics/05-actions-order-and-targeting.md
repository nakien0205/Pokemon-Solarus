# C04 — Action Legality, Order, and Targeting

Priority: P1  
Status: C04 complete under focused validation
Required order: C04A, then C04B

## Objective

Turn decision requests into a validated, locked action queue and determine the
legal targets of each action. This package does not calculate damage or execute
move effects.

## C04A — Selection, Legality, PP, Struggle, and Order

Supported command payloads:

- Fight: move slot and target choice when the move requires one.
- Bag: Trainer-owned item and legal target.
- Switch: acting active slot and reserve party slot.
- Run: acting battler for a legal wild attempt.
- Mandatory replacement: empty active position and reserve party slot.
- Scripted end and abandon through explicitly authorized encounter policies.
- WildFlee only for a wild selector whose species/encounter policy explicitly
  enables it.

Validation sequence:

1. Match battle/state/request versions and decision owner.
2. Confirm phase and action allowance.
3. Confirm acting battler is active, living, controlled by the Trainer, and not
   already committed.
4. Confirm command legality under the encounter policy.
5. Confirm move/item/reserve exists and is currently usable.
6. Confirm targets through C04B.
7. Lock the typed action. Consumption happens later at its documented execution
   point, not during selection.

Move usability:

- Moves with zero PP remain in the snapshot with a typed unusable reason.
- Selecting Fight with no move having PP creates Struggle.
- Bag, Switch, and Run remain available when independently legal.
- Captured-target cancellation and other actions canceled before execution do
  not consume PP.
- Obedience is a policy check at the documented action point from B00B; it does
  not permit an enemy selector to read the player's unexecuted command. It uses
  only C03's frozen obedience facts, consumes RNG/resources exactly as B00B
  specifies, and emits typed obey/disobey/result events.
- Modern friendship combat bonuses remain disabled.

Order:

- Freeze the complete turn order only after required Trainers/selectors have
  supplied decisions.
- Apply command/move priority, then effective Speed, then tie rules.
- A tie between different sides favors the player side.
- If all four active battlers tie, both player-side battlers precede both
  opponent-side battlers.
- Same-side ties consume one injected random tie roll per tied ordering group.
- An AI-controlled partner belongs to the player side for cross-side ties.
- Once locked, later Speed/stat refreshes do not reorder that turn.
- Emit `ActionOrderLocked` with every ordered action and its resolved ordering
  keys without exposing hidden choices to an unauthorized observer.

## C04B — Targets, Redirection, and Doubles

Target classes:

- Self, selected ally, selected opponent, any selected battler, random legal
  opponent, user side, opponent side, both sides, field, and fixed spread set.

Rules:

- Empty, fainted, captured, or removed positions cannot be newly selected.
- A selected semi-invulnerable battler remains selectable; reachability is
  checked only when the move resolves.
- If a selected single-target opponent faints before resolution, redirect to
  the other living opponent when one exists, exactly as the Solarus rule states.
- If the selected target was captured, cancel the queued targeted action. Do
  not redirect it or consume PP.
- Spread moves calculate one affected-position set, including allies when the
  move's target class requires friendly fire.
- Redirection effects apply at the B00B trigger point and cannot produce an
  illegal target.
- The player explicitly chooses Struggle's target in Doubles.
- Random targets use the injected RNG only after confirming a non-empty legal
  set.

## Resource-Consumption Contract

| Situation | Action consumed | PP/item consumed | RNG consumed |
|---|---:|---:|---:|
| Invalid/stale selection | No | No | No |
| Blocked Trainer Run | No | No | No |
| Legal failed wild Run | Yes | Not applicable | Yes |
| Move canceled because target was captured | Yes; the queued action is canceled | No PP | No move rolls |
| Move user faints before acting | Committed action ends | No PP | No move rolls |
| Move begins execution, then misses/fails | Yes | PP yes | Required checks only |
| Item blocked before legal use | No | No | No |
| Legal item use whose effect is prevented | Yes | Item yes | Required checks only |

If B00B records a more specific canonical exception, document it beside the
affected action type and add a golden test.

## Tests

C04A:

- Every command accepted/rejected in every phase.
- PP zero, some usable PP, no usable PP, and generated Struggle.
- No consumption for invalid, stale, blocked, fainted-before-action, and
  captured-target-canceled paths.
- Priority and Speed ordering, cross-side ties, all-four ties, same-side random
  ties, Quick Claw hook keys, and deterministic replay.
- Every standard-obedience outcome, its RNG/resource behavior, and proof that
  enemy observations never gain access to the selected command.
- Locked order unchanged by later stage/status/stat refresh.

C04B:

- Every target class in Singles and Doubles.
- Empty/fainted target rejection.
- Fainted-target redirection versus captured-target cancellation.
- Ally support, friendly-fire spread, redirection, random target, and Struggle
  choice.
- Semi-invulnerable selected, unreachable at resolution, and returned before
  resolution.

## Acceptance

- Identical setup, decisions, and RNG produce the identical ordered queue and
  targeting events.
- No test reaches into state internals to manufacture legality.
- C05 receives a validated locked action and target set and makes no independent
  selection-policy decisions.

## C04A Execution Status

- C04A completed on 2026-08-21 with final focused run ID
  `C04A-Actions-Exit-20260821T015813Z`.
- The final `PokemonSolarusEditor Win64 Development` module build
  (`C04A-EditorBuild-Suffixed-20260821T015247Z`) succeeded. The exact
  `PokemonSolarus.Battle.C04A` filter then discovered and performed seven tests:
  7 succeeded, 0 with warnings, 0 failed, and 0 not run; process exit code 0.
- The engine now validates complete normal-turn selections before locking one
  immutable queue. Queue keys contain the Solarus command band, integer move
  priority, fractional-priority tenths, effective Speed, acting slot, and the
  frozen reverse-Speed flag. Same-side exact ties consume one traced `U[0,1]`
  draw per two-battler group; exact cross-side ties consume no draw and put the
  player side first.
- Zero-PP authored moves remain visible with typed `NoPP` reasons. When every
  defined move has zero PP, the request exposes only the engine-owned modern
  Struggle fallback. Ordinary PP is deducted only after the action-start and
  future pre-move gates; Struggle has no PP.
- The action-start seam revalidates the actor and the captured selected target,
  then applies the accepted Solarus obedience rule before PP or RNG. It emits
  typed obey/refuse results, and enemy snapshots do not reveal the player's
  unexecuted choice.
- `ActionOrderLocked` events carry complete order metadata and are `CoreOnly`,
  so the authoritative replay can reproduce the queue without exposing hidden
  selections. Replay schema 3 serializes those keys in explicit field order.
- The seven public-seam tests cover command legality, typed zero-PP options,
  generated Struggle, command/move priority, Speed, all-four and cross-side tie
  rules, generic `+0.1` keys, frozen order, actor/captured-target start gates,
  deterministic obedience, PP timing, hidden-choice filtering, and canonical
  replay equality. No C04A test includes or mutates private battle state.
- C04A freezes only the generic fractional-priority and reverse-Speed inputs.
  Quick Claw eligibility, its `U[0,4]` item draw, and item reveal remain C08C;
  Trick Room state remains C07D. The current engine therefore supplies ordinary
  `0.0` fractional priority and non-reversed Speed until those owning packages
  connect the frozen hooks.
- At C04A completion, C04B had not been drafted or implemented. Its later
  completion is recorded below. C05 hit, damage, and move-effect execution
  remain out of scope for C04.
- The original C04A execution honored the user's validation limit and ran only
  the C04A-focused filter. A separately approved source cleanup on 2026-08-21
  then repaired the older unity incompatibilities without changing the validated
  C04A source or test files. `EBattleMoveCategory` now has one canonical
  definition in `Public/Battle/BattleMoveCategory.h`; older production helpers
  have file-specific names; and older test translation units use file-specific
  namespaces instead of colliding anonymous-namespace declarations.
- The cleanup passed a full forced-unity Editor build with adaptive exclusions
  disabled (`C04A-UnityCleanup-Pass3-20260821T022230Z.log`) and a normal default
  adaptive-unity Editor build
  (`C04A-UnityCleanup-DefaultAdaptive-20260821T022251Z.log`). No permanent unity
  override was added: `BuildConfiguration.xml` is empty, and the module rules,
  `.uproject`, and `DefaultEngine.ini` retain their protected hashes.
- Focused cleanup reports are under
  `Game/Saved/Automation/C04A-UnityCleanup-20260821T022425Z/`. The affected
  Damage Calculator, C01B, Random, C02A Permanent Stats, C02B, C03A, and C03B
  filters plus the unchanged C04A filter performed 44 tests: 44 succeeded, 0
  with warnings, 0 failed, and 0 not run. This was not a full
  `PokemonSolarus.Battle` suite run.
- All 11 C04A source/test files listed below still match their final C04A
  hashes after the cleanup.
- The user's open Editor held the ordinary module DLL during the final link, so
  the successful verification used Unreal's suffixed-module build path instead
  of terminating the Editor or risking the open map.
- Successful evidence is under
  `Game/Saved/Automation/C04A-Actions-Exit-20260821T015813Z/`, with logs
  `Game/Saved/Logs/C04A-Actions-Exit-20260821T015813Z.log` and
  `Game/Saved/Logs/C04A-EditorBuild-Suffixed-20260821T015247Z.log`.

Relevant C04A source hashes:

| File | Pre-run SHA-256 | Final SHA-256 |
|---|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleActionQueue.h` | absent | `47ee482163bc96347e71bd706e6495d5c5c260a8b45391d950e46c37b703ce07` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleActionQueue.cpp` | absent | `4a81d49495c57e0f73ba2f0311e12100d7160a85ada96bb283363576cb8ffddc` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleActionQueueTests.cpp` | absent | `428723b015a70a3a868b23a2bc740cc000c22e44636d6df0174b011c196fdc21` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEngine.h` | `9287a5ea93981c0fe94479912180d00f51bf88fa15ed08677ac77d1e8b6ed98b` | `8e63583b7a936153627f257077cef08440390d16d903979b6d513bb611d7592b` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp` | `89c4295eddebcaea17be43b3fa14cf9cb67f905418c8c017c494fc99b3443003` | `15f42f501293902d4e1f907b79e635f52056f559bf3f1c12c5fd2bd8cf6bc779` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEvent.h` | `b80fccbfe8185272cc655af41f1f4a187a0843ca50e68a08de28eb9291b0754a` | `f4481199de3137cd52f532a91fd08fc40378abc82f34466062fb5ee362dfb912` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEvent.cpp` | `960d346df2c504b54ae06b53697e8d659ca8f4c7994a51e4664a77e1cbe73e5f` | `e0670ef72099bb105474980b82fde59f22e8b54132100663cdbd9f83cb871fbd` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleReplay.h` | `6673440c37d4203a3c7a18db8bcb6f0992ee694e42693d566703a2d0e9f33eb9` | `313d0287df88b65b6452cc32f6fcbb3576abb1f09c137f539dc129e5e1e4fa76` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleReplay.cpp` | `951ed3de62064f27100ef9b76fc646f801dd36aeff3c1eb0a6b253a39e1bdf30` | `ad91e47799d3e80da9fe07a507b4cefe6b709d93aa161775ec4ec01c5d61d5e6` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.h` | `ba8b7a862862c30e28f0b4c5937bc27267cb2e9756def9856804b0ec1a8eed8e` | `37e9da7ef3bf32eba2b1b81d4d4f140d2d3678c11c8afd0e1a2f860d7b7cc341` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp` | `34d9f76eb52a878a5152190f9ff283f373fd12d14b0b57822b8da50863779547` | `d0704de211f741f25933ee850ab3a45651d8967f42a309f2ef38fe16cc361c92` |

Protected files matched their pre-run hashes after the final C04A run:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/PokemonSolarus.Build.cs` | `5055df3ec3790fb34ef6113f2f47ebe1bdafb0cda2e3ae3cd5b5e631a216d8b7` |
| `Game/PokemonSolarus.uproject` | `97d07ae09b7fbcb7e095ebfd5a4a15c1e1c2953e40e93ec2c89bf407257af7e5` |
| `Game/Config/DefaultEngine.ini` | `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920` |
| `plan/battle_mechanics/reference/modern-rules-snapshot.md` | `ded20d707ab67c2bb4d883df8ac07cbd20e38a31766b8a9ef14515c93168ad50` |
| `docs/battle-system-interview-handoff.md` | `476040bcaf0cfdb9d7f97d3fba3ddc752f7760de252bcc7a8775f846a58c98a6` |

## C04B Execution Status

- C04B completed on 2026-08-21 with final focused run ID
  `C04B-Targeting-Final3-20260821`.
- The final normal adaptive non-unity build
  (`C04B-EditorBuild-Final3-20260821.log`) and forced-unity build with adaptive
  exclusions disabled (`C04B-EditorBuild-ForcedUnity-Final3-20260821.log`)
  both succeeded for `PokemonSolarusEditor Win64 Development`.
- The exact `PokemonSolarus.Battle.C04B` filter discovered and performed seven
  tests: 7 succeeded, 0 with warnings, 0 failed, and 0 not run; process exit
  code 0. No older or future Automation filter was run.
- Selection and resolution use exactly four canonical structural positions:
  Player Left, Player Right, Opponent Left, and Opponent Right. All ten target
  classes produce stable candidate or automatic target sets. Empty, fainted,
  captured, and removed positions cannot be newly selected; living
  semi-invulnerable battlers remain selectable because C05 owns reachability.
- Only selected ally, selected opponent, and any selected battler require an
  explicit choice. Self, random opponent, sides, field, and fixed spread are
  automatic. Fixed spread excludes the user and includes every other living
  structural position, including a living ally. Doubles Struggle remains an
  explicit selected-opponent action.
- Capture of the originally selected battler cancels at action start before PP
  or move RNG. Normal target resolution occurs only after the move commits and
  PP is spent. A fainted selected opponent falls back deterministically to the
  other living opponent; a non-empty random-opponent set consumes exactly one
  injected `Rule.Targeting.RandomLegalOpponent` draw, including `U[0,0]` for a
  one-candidate set, while an empty set consumes no draw.
- The locked action freezes the target class and a validated typed target set
  for C05. `TargetsResolved` events carry complete battler identity triples or
  canonical side/field tokens. Replay schema 4 serializes automatic request
  classes and typed target events in explicit field order. Two identical
  public-engine runs produced the same queue, targeting events, RNG trace, and
  canonical replay bytes.
- Ordered legal redirection proposals are frozen and tested in the pure
  resolver. The engine intentionally supplies no ordinary proposals until the
  later Ability/condition packages own their generation. Capture cancellation
  still wins before any proposal.
- No public C04B API can yet perform a voluntary switch or mutate a living
  active battler into the post-lock no-legal-target state. Public engine
  regressions for replacement-slot targeting and post-PP no-target completion
  therefore remain deferred until the owning later package exposes those
  transitions; production handling exists without granting tests private-state
  access.
- C04B's replay additions require schema 4. The single pre-existing C04A
  schema-version assertion was updated from 3 to 4 so the committed tree does
  not retain a known stale expectation. The C04A filter was not run, so no
  fresh runtime claim for it is made here.
- Successful evidence is under
  `Game/Saved/Automation/C04B-Targeting-Final3-20260821/`, with logs
  `Game/Saved/Logs/C04B-Targeting-Final3-20260821.log`,
  `Game/Saved/Logs/C04B-EditorBuild-Final3-20260821.log`, and
  `Game/Saved/Logs/C04B-EditorBuild-ForcedUnity-Final3-20260821.log`.

Relevant C04B final source hashes:

| File | Final SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleTargeting.h` | `c446f63b0d7e5b36924bfdac94af53043db562cb1dbea0520dcd6d7ef409886b` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleTargeting.cpp` | `20ad8b3d2723c9bfbb1fc03c600d2d78d219e3e1944a2371a9e2f8306b3ad7cf` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleDecision.h` | `e4721d8f35a3aa58aecdf7ba442df4a2a8aba768fa7fbc926dd531f484c2160e` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDecision.cpp` | `9ddc3258d58891deefca2ac8e48d0584830e6dde0520b67b9d67c56d5a18364d` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleActionQueue.h` | `5508fe9b9d55c376fcca2fda1bc37af51ae05726d87f7784bf31fb56a3b048dc` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleActionQueue.cpp` | `e698b0e7e3dc7c5201b44943602ee706bc90a6157af5ac6f08b134b39e0274a1` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEngine.h` | `4b351872b2c6d891e4819cef321dabb0060ef7d885c672de1ee2264214106570` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp` | `951be6500c30e87b962c0a089176064b0ca4a8d689189039794edeeeabd7a836` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEvent.h` | `aaf0a5b3ea99563a3a8ce62a8ef56ab3ec507806a6c54a883e466ce061b7f086` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEvent.cpp` | `3df1b166cc0bc86e86176f73d1557960c29230f87b6bbfc9e38b222d2c1f1b92` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleReplay.h` | `d1cad6df30c07af55aaac1f9442eeb6f8e9fd1d65c0300fc82b71555ad120ef7` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleReplay.cpp` | `b35f1b327c2ab9f12559bd4a55bf50076d952dad4ed4b1f2b2ec50cb9a96cab7` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.h` | `5e0efdda6b0d3367875ada8e7b807af17329b7ab76052bc740691f18bc593ae9` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp` | `6e96a96f3decfe44dabf2dcbf7430efd7d2bc8e3b52634ea2b6e1e7565ab7328` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleTargetingTests.cpp` | `a78f3476ff19026632251bdf94a99c8c8166a2b3cfe34891b243f3312acee85b` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleActionQueueTests.cpp` | `363cd88c56d9971071c982524dd37162338aa58f049ce57b6153292850cccafd` |

Protected files matched their pre-run hashes after final C04B validation:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/PokemonSolarus.Build.cs` | `5055df3ec3790fb34ef6113f2f47ebe1bdafb0cda2e3ae3cd5b5e631a216d8b7` |
| `Game/PokemonSolarus.uproject` | `97d07ae09b7fbcb7e095ebfd5a4a15c1e1c2953e40e93ec2c89bf407257af7e5` |
| `Game/Config/DefaultEngine.ini` | `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920` |
| `plan/battle_mechanics/reference/modern-rules-snapshot.md` | `ded20d707ab67c2bb4d883df8ac07cbd20e38a31766b8a9ef14515c93168ad50` |
| `docs/battle-system-interview-handoff.md` | `476040bcaf0cfdb9d7f97d3fba3ddc752f7760de252bcc7a8775f846a58c98a6` |

C04 is complete under the approved focused-validation scope. C05A is
dependency-clear and is the next sequential package.
