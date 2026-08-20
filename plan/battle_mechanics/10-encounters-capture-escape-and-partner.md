# C09 — Encounters, Capture, Escape, Reinforcement, and Partner Battles

Priority: P2/P3  
Status: Blocked by C06B, C08B, and C08C  
Required order: C09A, C09B, then C09C

## Objective

Compile encounter kind and format into deterministic legality/outcome policies,
then implement wild and partner-specific flows without persistent storage or
strategic AI.

## C09A — Encounter Policies and Selector Boundary

Encounter kinds:

- Wild.
- Ordinary Trainer.
- Rival, using Trainer rules with a distinct tag/profile.
- Boss/Gym, using Trainer rules plus explicit item/script permissions.
- Tutorial/Scripted.

Formats:

- Single, Double, PartnerDouble.
- Maximum two active battlers per side and party size six.
- Reject unsupported Triples, quadruples, and invalid owner/format shapes.

Compile setup into typed policies for Run, capture, Bag, Revive, Shift/Set,
reinforcement, configured wild fleeing, scripted ending, partner ownership, and
selector profile.

Define `IBattleActionSelector`:

- Input: filtered immutable observation plus already generated legal actions.
- Output: one selected legal action ID/payload.
- Enemy observations omit the player's unexecuted selection.
- Partner observations may include the player's selected action.
- Every result is revalidated through the same core path.
- Provide one deterministic scripted selector for tests only.

Wild/Basic/Skilled/Boss/Tutorial/Partner profile tags are allowed. Strategic
scoring, cheating policies beyond explicit setup, team authorship, and tuning
remain outside this roadmap.

## C09B — Run, Capture, and Cry for Help

### Run

- Trainer encounters reject Run immediately without consuming action, item, or
  RNG.
- Wild Run uses the acting player Pokemon and the living wild Pokemon in the
  leftmost occupied opponent slot.
- Use permanent unmodified Speed: ignore stages, paralysis, Ability, item,
  weather, terrain, and other transient modifiers.
- Reject invalid calculated Speed below four before evaluating the formula.

```text
F = floor((PlayerSpeed * 32) / floor(WildSpeed / 4)) + 30 * C
```

- `C` starts at `1`, increments after each legal failed Run attempt, and never
  resets during that battle, including after other actions or switching.
- If `F > 255`, succeed without an RNG roll.
- Otherwise draw a uniform integer `R` in `0..255`; succeed if `R < F`.
- A legal failed attempt consumes the action. A blocked attempt does not change
  `C`.

### Capture

- Only player-owned Trainers may use Poke Ball and only in capture-legal wild
  encounters.
- Select exactly one living wild target; at most one ball per Trainer action.
- Before consumption, validate target, encounter policy, ball count, and
  combined party/storage capacity including pending captures.
- Standard Scarlet/Violet math consumes immutable inputs: species catch rate,
  current/max HP, status, ball multiplier, wild/player levels, badge/obedience
  snapshot, caught-species/critical-capture snapshot, charm/power/back-strike or
  other supported modifiers. Proof content uses neutral values except Poke
  Ball's standard multiplier.
- B00B defines every integer/scale step and shake/critical RNG call.
- A failed legal capture consumes ball and action.
- Success removes only the target, cancels its queued action and every queued
  move specifically targeting it, redirects none, and consumes no PP for the
  canceled moves.
- Surviving wild opponents keep the battle active; several wild Pokemon may be
  captured over several turns.
- Store a pending captured snapshot with captured HP, status, PP, and original
  held item. It cannot join the same battle.
- Emit an ordered pending destination request; external party/storage performs
  the actual write after battle.
- Capturing the last wild opponent produces Victory with capture cause.
- Captures already completed remain in the final result even if the player later
  escapes or loses.

### Cry for Help

- Available only when the second wild active slot is empty and the one-success
  battle limit has not been reached.
- Attempt consumes caller action and PP.
- Use one uniform success check with exactly 80% probability.
- Failure leaves the slot empty; success creates the configured reinforcement
  in that slot and permanently disables further successful calls this battle.
- The summon cannot act until the next turn and may later be captured.
- Third/fourth reinforcement formulas are explicitly excluded.

### Configured wild-opponent fleeing

- Default is disabled. Only a species or encounter with an explicit authored
  flee policy may make this choice.
- The policy supplies its trigger/eligibility and optional probability through
  normal action/effect data; the core never invents a flee chance.
- A legal flee action consumes that wild battler's action, removes that battler,
  and cancels its later queued action without PP consumption.
- If another wild opponent remains, the battle continues. If none remains, end
  with `Escape` and cause `OpponentFled`; do not award ordinary Victory merely
  because the opponent left.
- Emit removal/outcome facts for later rewards/progression; core calculates
  neither.

## C09C — Partner Double Battles

- Player and partner are separate Trainers with separate parties, Bags, action
  allowances, ownership, switches, and selector assignments.
- Resolve Ask/control configuration before battle and freeze it in setup.
- Changing control changes only the selector, not ownership or rules.
- Support moves targeting an allied battler while preserving owner-party item
  restrictions.
- Partner Trainers cannot capture; the player cannot fill an exhausted partner
  slot with a player reserve.
- Partner-selected reserves must belong to the partner.
- If all player Pokemon faint but the partner can continue, the battle remains
  active under Team Victory rules.
- On partner Team Victory, restore the first valid player Pokemon to 1 HP and
  cure its major status through typed result events.
- NPC partner Pokemon are marked ineligible for persistent EXP/EV through
  external result facts; core performs no reward calculation.

## Tests

C09A:

- Every encounter kind/format policy matrix and unsupported setup.
- Selector visibility, legal-only selection, stale/invalid revalidation, and
  deterministic scripted selector.
- Default-disabled and explicitly configured wild-flee policies.

C09B:

- Trainer Run rejection; `F > 255`; exact `R < F` boundaries; failed attempt
  increments; other actions/switches do not reset; blocked attempt unchanged;
  invalid Speed rejected.
- Capture capacity block, failure, success, multiple captures, last capture
  Victory, retained captured state, queue cancellation, no redirection/no PP,
  and final pending destination order.
- Normal/critical capture RNG golden vectors from B00B.
- Cry for Help fail/success/boundary, one-success limit, next-turn action, and
  captured summon.
- Configured wild flee with one/multiple opponents, authored probability,
  queued-action cancellation, continued battle, and OpponentFled outcome.

C09C:

- Separate commands/Bags/switches, partner visibility, ally support, illegal
  cross-owner item/switch/capture, partner slot exhaustion, player wipe with
  partner continuation, Team Victory, and 1-HP/status-cure result.

## Acceptance

- Wild and partner flows replay identically from the same setup/actions/RNG.
- Core emits all external persistence/reward facts but performs no write or
  reward computation.
- C09B and C09C are never implemented concurrently because both integrate the
  active-slot and engine flow.
