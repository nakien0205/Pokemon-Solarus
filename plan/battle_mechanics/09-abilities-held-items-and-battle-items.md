# C08 — Abilities, Held Items, and Battle Items

Priority: P2  
Status: C08A/B/C complete; C10A R5 held-item move extension complete

Required order: C08A; then C08B/C

## Objective

Add reusable Ability and item hooks, deterministic reveal/trigger ordering, and
Trainer-owned Bag actions. Populate only the approved proof sets.

## C08A — Shared Trigger and Ownership Contracts

Ability/item hooks may:

- Modify action eligibility, priority, Speed, accuracy, target reachability,
  type immunity, damage phases, status/effect application, switch-in/out, item
  use, end-turn effects, faint prevention, and field creation.
- Suppress, ignore, reveal, consume, restore, remove, swap, or temporarily steal
  state only through typed effect requests.

Ordering:

- Follow B00B's canonical phase and ordering keys.
- Use stable battler/side/position/creation IDs only after canonical tie rules.
- Reveal an Ability or item only when its rule normally becomes public.
- Hidden activation failures cannot leak an unrevealed Ability/item through
  snapshots, selector diagnostics, or events.

Held-item ownership:

- Store original owner/item separately from current transient state.
- Normal consumption remains consumed after battle.
- Recycle restoration remains restored.
- Temporary theft, swapping, removal, or suppression resets to original
  ownership after battle.
- A captured wild Pokemon keeps its original held item.
- Battle-generated items disappear after battle.
- A successful in-battle consumption records the consumer Trainer/battler and
  its monotonic fact ordinal. Recycle selects the greatest matching ordinal
  among items that are still consumed.
- A persistent item already consumed before setup has no battle-consumption
  history and is not eligible for Recycle.
- Core emits final item-state facts; it does not write inventory.

Bag ownership:

- Each Trainer has a finite battle Bag snapshot.
- One Bag action per Trainer per turn, including a Trainer controlling two
  active Pokemon.
- Player and partner Bags are separate.
- Items target only legal members of the owner's party unless an explicit item
  says otherwise.
- A pre-use rejection consumes nothing. A legal use whose effect is later
  prevented consumes item/action when B00B says it does.

### C08A Completion Record

- C08A completed on 2026-08-22 from `main` baseline
  `7f6503598849cb2b5efb4a33b097b9950a4fde4a`, while preserving the
  pre-existing `FoundationMap.umap` modification.
- `BattleAbilityItemContracts` now defines the shared semantic hook vocabulary,
  typed Ability/item effect requests, a validated bridge into C07A's scheduler,
  and public-safe first/repeat reveal facts that emit nothing for hidden
  ineligible, suppressed, ignored, or non-public prevented activations.
- The held-item ledger stores stable item-instance identity, persistent origin,
  original owner/item, and current transient state separately. Suppress,
  reveal, consume, restore, remove, swap, and temporary-steal operations are
  typed and atomic; battle-end facts reset temporary ownership, preserve normal
  consumption or Recycle restoration, keep a captured original owner's item,
  and remove battle-generated items without writing inventory.
- Trainer Bag snapshots remain separate and finite. Each Trainer owns one
  battle-local action quota per turn; pre-use rejection consumes nothing, while
  a legal use consumes one item and the action even when its effect is later
  prevented.
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

## C08B — Ability Proof Set

- Blaze: low-HP Fire damage modifier.
- Overgrow: low-HP Grass damage modifier.
- Intimidate: opponent Attack stage changes on entry with proper ordering.
- Levitate: Ground immunity and grounded-state interactions.
- Drizzle: Rain creation/replacement on entry.
- Speed Boost: end-turn Speed stage increase.
- Magic Guard: indirect-damage prevention without suppressing direct damage.
- Mold Breaker: ignore eligible defensive Abilities with correct public reveal.

Each Ability must use general hooks rather than species-specific branches.

### C08B Completion Record

- C08B completed on 2026-08-22 from `main` baseline
  `7f6503598849cb2b5efb4a33b097b9950a4fde4a`, with the completed C08A
  worktree and pre-existing `FoundationMap.umap` modification preserved.
- `BattleAbility` defines exactly Blaze, Overgrow, Intimidate, Levitate,
  Drizzle, Speed Boost, Magic Guard, and Mold Breaker. Their reusable C08A/C07A
  hooks own activation, reveal, suppression, deterministic ordering, and
  cleanup; no species-specific branch was added.
- The live engine registers starting and incoming active Ability hooks, resolves
  entry effects by effective Speed, applies per-hit offense and eligible
  defensive bypass, integrates grounded/hazard/terrain and residual behavior,
  and cleans Ability hooks on switch and faint boundaries.
- `AbilityActivated` is an appended public event type. Existing event ordinals
  and replay schema remain stable, while first/repeat public reveal facts remain
  source- and owner-specific.
- The eight proof Abilities cover their approved positive, ineligible,
  suppressed, ignored, ordering, reveal, switch/faint-cleanup, and interaction
  paths. Magic Guard covers all currently implemented indirect-damage families,
  including Confusion self-hit, while direct move damage and Substitute HP cost
  remain unblocked.
- The final all-source `PokemonSolarusEditor Win64 Development` build succeeded
  with `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA` and exit
  code `0`:
  `Game/Saved/Automation/C08B-Abilities-Final-20260822T100431Z/build-single-source-unity.log`.
  A separate packed-unity attempt without the one-source split exposed only
  pre-existing ambiguous helper names between untouched
  `BattleActionQueueTests.cpp` and `BattleFieldSideConditionTests.cpp`; C08B did
  not modify those older tests.
- Only `Automation RunTests PokemonSolarus.Battle.C08B` filters were run. The
  final exported report at
  `Game/Saved/Automation/C08B-Abilities-Final-20260822T100431Z/report-final/index.json`
  records exactly 20 succeeded, 0 succeeded with warnings, 0 failed, 0 not run,
  and 0 in process; every entry has 0 warnings and 0 errors, every path is under
  the C08B filter, and the process exited `0`. The matching command log is
  `Game/Saved/Automation/C08B-Abilities-Final-20260822T100431Z/automation-final.log`.
- C08A source/tests, concrete C08C items, catalogs/Data Tables, assets, UI,
  configuration, module rules, the `.uproject`, existing tests, the B00B
  snapshot, and the Solarus interview handoff were not modified. No
  `dev-story`, commit, older battle filter, full battle suite, or project-wide
  test run was used.
- C08B is complete under the approved focused-validation scope. C08C is
  dependency-clear; C09 still requires C08C.

## C08C — Held-Item and Battle-Item Proof Sets

Held items:

- Leftovers: end-turn healing.
- Sitrus Berry: threshold-triggered consumption/healing.
- Lum Berry: status/eligible volatile cure and consumption.
- Focus Sash: full-HP faint prevention and consumption.
- Life Orb: damage modifier plus post-damage recoil.
- Choice Band: Attack modifier plus move lock and switch cleanup.
- Heavy-Duty Boots: entry-hazard immunity.
- Air Balloon: Ground immunity, public reveal, and pop-on-hit behavior.
- Quick Claw: action-order activation and RNG/visibility rules.

Battle items:

- Poke Ball: wild-target capture action through C09.
- Hyper Potion: heal exactly 120 HP, capped at Max HP; reject full-HP/fainted
  targets before consumption.
- Revive: legal fainted owner-party target and B00B's modern restored HP.
- Full Heal: cure exactly the major-status and volatile set frozen by B00B; do
  not assume it is limited to major status.
- X Attack: active user target and modern Attack-stage increase.

C08C rejects Revive for every opponent Trainer. C09A owns any later explicit
boss permission. Partner Trainers cannot capture.

### C08C Completion Record

- C08C completed on 2026-08-23 from `main` baseline
  `61b2d8f16e1dfbf245e84baa11a2cc20177ac861`, while preserving the unrelated
  dirty map and concurrent UI-asset work.
- `BattleItem` defines the nine approved held items through reusable C08A/C07A
  hooks. The live executor and engine cover recovery, status cure, per-hit
  Focus Sash, damage modifiers, Life Orb recoil/faint, Choice lock and generic
  no-leak cancellation/cleanup,
  hazard/Ground interactions, Air Balloon reveal/pop/known-empty projection,
  Quick Claw ordering/RNG, suppression, and deterministic trigger ordering.
- The held-item ledger proof covers consumption and Recycle restoration,
  Knock Off-style removal, Trick-style swapping, Thief-style temporary theft,
  captured-original ownership, and battle-generated cleanup without a
  persistent inventory write.
- `BattleBagItem` defines exactly Poke Ball, Hyper Potion, Revive, Full Heal,
  and X Attack. Selection publishes exact item/target pairs, then execution
  revalidates stale actions before consuming the acting Trainer's copied item
  count and per-turn Bag quota. Player and partner Bags remain separate.
- Hyper Potion heals 120 HP capped at Max HP; Revive restores
  `max(1, floor(MaxHP / 2))`; Full Heal clears all six canonical major statuses,
  Confusion, and Toxic's private trigger counter while retaining unrelated
  volatiles; X Attack raises the acting battler's Attack by two stages capped at
  `+6`. Pre-use rejection and stale revalidation consume no item, Bag quota, or
  RNG.
- Option A was approved for the unresolved boss boundary: C08C rejects Revive
  for every opponent Trainer. C09A owns any later explicit boss permission.
  Partner capture remains rejected. A valid Poke Ball action is frozen as a
  true no-op C09 handoff, with no capture math, consumption, RNG, history, or
  counter mutation in C08C.
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
- The C08C implementation did not modify C09 capture calculations, capacity,
  removal, completion, persistent inventory writes, catalogs/Data Tables,
  assets, UI, configuration, module rules, the `.uproject`, non-C08C tests, the
  B00B snapshot, or the Solarus interview handoff. No `dev-story`, commit,
  older battle filter, full battle suite, or project-wide test run was used.
- C08 is complete under the approved focused-validation scope. C09A is now
  dependency-clear; C09B and later packages remain dependency-blocked.

### C10A R5 Held-Item Move Extension Completion Record

- At R5 closeout on 2026-08-30, no C10A source row had been authored. C10A later
  authored the approved item and move rows. The generic operations are
  `RemoveCurrent`, `ExchangeCurrent`,
  `TransferCurrent`, and `RestoreLastConsumed`; no move, item, species, or match
  ID controls generic behavior.
- `BattleHeldItemMoveEffects` owns stateless operation validation and the
  data-driven takeability rule. Knock Off receives Q12 `6144` only when the
  current target item can be taken by a move; item suppression does not change
  takeability.
- `BattleEffectExecutorItemMoves` resolves live item instances and stages the
  existing ledger operations together with battler mirrors, hook ownership,
  direct reveal state, and Choice-lock cleanup. Item intents run after generic
  effects and before forced switches and starting-Life-Orb recoil in the one
  existing staged execution plan.
- The ledger records last-consumer Trainer/battler identity plus the successful
  consumption fact ordinal. Recycle restores the greatest matching ordinal.
  Persistent items consumed before setup may have empty history and are
  excluded from lookup; incomplete, generated, or restored history-less states
  remain invalid.
- Public `ItemRestored = 55` and `ItemTransferred = 56` events append to the
  existing event vocabulary. Their item/source/target/numeric/visibility shapes
  and deterministic bytes are validated under unchanged replay schema `6`.
- The final forced-Unity build and all reports are rooted at
  `Game/Saved/AutomationReports/R5-HeldItemMoves-Final-20260830-091817`.
  The focused `PokemonSolarus.Battle.C08C.C10HeldItemMoves` filter passed 12/12.
  The required affected filters passed serially with counts
  `7, 34, 35, 7, 8, 8, 9, 7, 20, 39, 6, 18`; all issue counters are zero.
  There are 210 overlapping successful executions and 198 unique paths.
- Build-log and linked-DLL SHA-256 values are
  `DE3D70FB5EB3C7C9D01DD1E5FEE376655E5EAEE85CA7F1953B158B9AA58A103C`
  and `37E73305CA4E5CD9D9881BF12E565B2C5E7B2AD49AB4319C77BA3B726E165ED1`.
  Counter, exact-path, and source-hash manifest SHA-256 values are
  `55412CA9E40EDAB0440403DD9C46E6DDC32CD41817C8F6456537F77A201AC165`,
  `45090B742F815CA44431B0F96FEB4774C605330C7F840C252E87150C1AAC15BC`,
  and `E2AFED5A8C2A8161FA11B65A5843641CED4D1F68E68354442744669331B2110A`.
- Final `code-review` was APPROVED and final `test-evidence-review` was
  ADEQUATE/COMPLETE with no remaining finding. At R5 closeout, no Git action,
  persistent inventory write, visual asset change, or independent R7 had been
  performed.

## Safe Session Split

- C08A runs alone and owns shared hooks/ordering.
- C08B and C08C may be separate isolated lanes after C08A.
- One integration session owns modifications to shared engine, damage, order,
  switching, and end-turn files.

## Tests

- Every proof Ability/item activation, non-activation, reveal, suppression,
  order, switch/faint cleanup, and interaction with conditions/fields.
- Mold Breaker versus Levitate and non-ignorable effects.
- Heavy-Duty Boots versus every hazard; Air Balloon versus Ground and popping;
  Magic Guard versus each indirect-damage family.
- Quick Claw with move priority, Speed ties, Trick Room, and deterministic RNG.
- Focus Sash multi-hit behavior, Sitrus threshold timing, Lum cure timing,
  Life Orb recoil/faint, and Choice lock/Struggle/switch.
- Item ownership across consume, Recycle, Knock Off, Trick, Thief, capture, and
  battle-generated cleanup.
- Separate player/partner Bags, one-action quota, owner targeting, rejection,
  prevented legal effect, opponent Revive rejection with boss permission
  deferred to C09A, and no persistent write.

## Acceptance

- No approved Ability/item requires a bespoke engine branch keyed by species.
- Trigger/reveal ordering is deterministic and does not leak hidden data.
- Consumption and ownership facts are sufficient for a future inventory service
  without core owning that service.
- C09 can implement capture/partner flows using the frozen Bag and ownership
  contracts.
