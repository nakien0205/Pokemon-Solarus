# C04 — Action Legality, Order, and Targeting

Priority: P1  
Status: Blocked by C03  
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
