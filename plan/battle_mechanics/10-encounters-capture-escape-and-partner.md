# C09 — Encounters, Capture, Escape, Reinforcement, and Partner Battles

Priority: P2/P3  
Status: C09A complete; C09B session-1 Capture complete; C09B session 2 next;
C09C blocked by C09B
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

### C09A Completion Record

- C09A completed on 2026-08-24 from clean `main` baseline
  `2dbcc0122f027f53927744102f8d3503e8db238e`, after the user explicitly
  verified and accepted the production runtime/HUD slice.
- `BattleEncounterPolicy` now compiles every supported encounter-kind/format
  pair into immutable-by-interface typed policies for the required active and
  party limits, Run, capture, Bag, Revive, Shift/Set, reinforcement, configured
  wild fleeing, scripted ending, partner ownership, controller, selector ID,
  and selector profile tag.
- Wild opponent Trainer Bags are invalid. Run and capture are player-only and
  wild-only; configured fleeing is wild-only; right-slot reinforcement is
  exposed only for multi-active Wild formats; Shift is limited to eligible
  non-Wild Singles; scripted ending is Tutorial/Scripted-only; and separate
  partner ownership is PartnerDouble-only.
- Ordinary, Rival, and Tutorial opponents cannot use Revive. A Boss/Gym
  opponent receives Revive permission only from an explicit finite Bag entry,
  and the existing generated-action and execution paths enforce that authored
  count and owned fainted target.
- `IBattleActionSelector` receives a deep-copied observer-filtered snapshot plus
  one exact core-generated request. A selector result must pass the request's
  existing `Allows` validation, then normal engine submission performs final
  stale-state revalidation. The deterministic FIFO selector lives only under
  `Private/Tests`.
- The final `PokemonSolarusEditor Win64 Development` build succeeded with
  `-ForceUnity -DisableAdaptiveUnity -BytesPerUnityCPP=1 -NoUBA` and exit code
  `0`:
  `Game/Saved/Automation/C09A-PoliciesSelectors-Final-20260824T095839Z/build.log`.
- Only `Automation RunTests PokemonSolarus.Battle.C09A` was run. The exported
  report at
  `Game/Saved/Automation/C09A-PoliciesSelectors-Final-20260824T095839Z/report-final/index.json`
  records exactly 6 succeeded, 0 succeeded with warnings, 0 failed, 0 not run,
  and 0 in process. All six paths have the C09A prefix and every entry contains
  0 warnings and 0 errors. The process exited `0`; report SHA-256 is
  `c72f3b076d8f7fcec3af65ce3d4600a579d97221c7a2be18ad8f762e5fb3cd11`.
- An intermediate C09A-only rerun stopped on a test-fixture assertion while
  constructing an invalid seventh party slot and exported no report. The
  fixture was corrected before the final clean run and is not acceptance
  evidence.
- C09B/C09C flows, strategic AI/scoring, replay schema, persistence, assets, UI,
  configuration, module rules, the `.uproject`, non-C09A tests, B00B, and the
  Solarus handoff were not modified. No `dev-story`, commit, older battle
  filter, full battle suite, or project-wide test run was used.
- C09A remains complete. Current C09B session-1 status is recorded below; C09C
  remains blocked by C09B.

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
  current/max HP, status, ball multiplier, wild/player levels, and the dedicated
  capture-progression snapshot containing badge count, caught-species count,
  critical-capture permission, Catching Charm, caught-count HP-component mode,
  capture coefficient, and must-capture mode. Obedience data is not a capture
  input. Proof content uses Poke Ball's standard multiplier.
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

#### C09B session-1 completion

- Exact calculation/RNG, Poke Ball selection/execution, multiple captures,
  target removal and queue cancellation, ordered pending destinations, retained
  captured facts, public metadata, and last-capture `Victory/Capture` are
  implemented.
- The full shared C09B schema is frozen at replay schema `5`, including capture
  progression/capacity, species classification, configured reinforcement
  identity, escape-attempt count, reinforcement-success flag, and pending
  capture records. Session 2 does not require another schema-format change.
- The required Editor build succeeded; its final log is
  `Game/Saved/Automation/C09B-Capture-20260824T125438Z/build-final.log`. The
  final focused report is
  `Game/Saved/Automation/C09B-Capture-20260824T125438Z/report-final/index.json`:
  4 succeeded, 0 succeeded with warnings, 0 failed, 0 not run, and 0 in
  process; every test has 0 warnings and 0 errors.
- C09B session 1 complete; session 2 Run, Cry for Help, and WildFlee next.
  C09B as a whole remains incomplete, and C09C remains blocked.

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

- Session-1 Capture proof is complete under four
  `PokemonSolarus.Battle.C09B.Capture.*` tests.
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

The Run, Cry for Help, and configured WildFlee bullets above remain session-2
work and were not tested in session 1.

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
