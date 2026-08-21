# C04 — Action Legality, Order, and Targeting

Priority: P1  
Status: C04A complete under focused validation; C04B dependency-clear and not started
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
- C04B was not drafted or implemented. Full target-class resolution, fainted
  target redirection, spread sets, random targets, and redirection effects remain
  C04B. C05 hit, damage, and move-effect execution also remain out of scope.
- Per the user's explicit validation limit, only the C04A-focused filter was
  run. Earlier package filters and the full `PokemonSolarus.Battle` suite were
  not rerun, so this session makes no fresh runtime claim for them.
- Adding the new translation units caused Unreal's default adaptive-unity
  regeneration to group older non-C04A files that already declare duplicate
  `EBattleMoveCategory` and anonymous-namespace helpers. C04A was therefore
  compiled with a temporary ignored per-module non-unity override. That override
  was restored to the original empty `Game/Saved/UnrealBuildTool/BuildConfiguration.xml`
  after validation; no build configuration was committed. Repairing the older
  unity incompatibilities or permanently disabling unity is a separate,
  explicitly unapproved cleanup.
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
