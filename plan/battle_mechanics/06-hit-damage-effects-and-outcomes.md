# C05 — Hit, Damage, Reusable Effects, Fainting, and Outcomes

Priority: P1  
Status: Blocked by C03; C05B integration also requires C04B  
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

