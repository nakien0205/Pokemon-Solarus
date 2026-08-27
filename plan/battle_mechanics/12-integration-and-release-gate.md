# C11 — Full Integration and Release Gate

Priority: Mandatory completion gate  
Status: Not started; blocked by C10A, C10B, and final ADR-0002 implementation closeout

Required order: C11A, then C11B

## Objective

Prove that the complete bounded battle core works as one deterministic system,
not merely as passing isolated calculators. Produce evidence tied to exact live
source and data hashes.

## C11A — Deterministic Integration Matrix

### Baseline regression

- Existing four calculator tests still pass unchanged.
- Charizard/Venusaur retains the documented base-damage fixtures.
- Final-damage tests clearly distinguish base result from later modifiers.

### Single Battle

- Full setup, selection, legality, order, PP, hit, damage, effects, faint,
  replacement, end-turn effects, and terminal outcome.
- Standard obedience at every eligibility/result boundary with exact RNG and
  action/PP behavior.
- Voluntary switching, trapping, forced/pivot switching, Shift accepted/
  declined, Set policy, and no reserve.
- Every major status and selected volatile, including cross-interactions.
- Bag actions, Ability/item triggers, ownership cleanup, and blocked actions.

### Double Battle

- Four active battlers, cross-side and same-side Speed ties, priority, Trick
  Room, Quick Claw, and locked-order stability.
- Selected, redirected, random, ally-support, spread/friendly-fire, empty,
  fainted, captured, and semi-invulnerable targets.
- Simultaneous spread HP changes/faints, stable event grouping, queued actions,
  and one/two mandatory replacements.
- Two voluntary switches with distinct reserves and duplicate-reserve rejection.

### Modifier combinations

- STAB, dual typing, immunity, critical, random damage, burn, spread, weather,
  terrain, screens, stages, Ability, and held-item phases in B00B order.
- Blaze/Overgrow thresholds, Rain/Sun, Levitate/Mold Breaker, Magic Guard,
  Focus Sash, Life Orb, Choice Band, Air Balloon, and screen removal.
- Exact `FDamageTrace`, RNG trace, HP result, reveal events, and cleanup.

### Conditions and field

- Every approved weather, terrain, hazard, screen, room, and side condition:
  start, replacement/coexistence, duration/layers, active effect, removal,
  expiry, and snapshot visibility.
- Entry hazards combined with type, grounded state, Levitate, Heavy-Duty Boots,
  Air Balloon, switching, and switch-in faint.
- No recursive/double trigger and deterministic end-turn ordering.

### Wild encounter

- Blocked Trainer Run and successful/failed custom wild escape, including exact
  counter persistence and RNG boundaries.
- Poke Ball capacity rejection, normal/critical failure/success, multiple
  captures, retained captured state, target/action cancellation, no PP, last
  capture Victory, and pending destination order.
- Cry for Help and wild reinforcement are **Freeze until call by user**. They
  are excluded from the current release gate; existing related code stays
  unchanged until explicitly requested.
- Configured wild-opponent flee with default-disabled policy, one/multiple wild
  opponents, authored probability, and OpponentFled outcome.

### Partner Double Battle

- Separate player/partner selectors, parties, Bags, actions, ownership, legal
  switches, and ally targets.
- Exact request sequence: human player choices first, partner AI after those
  choices, and enemy selectors using a filtered pre-choice observation.
- Enemy hidden-choice boundary and partner visibility allowance.
- Illegal partner capture/cross-owner switch/item.
- Player party exhaustion with partner continuation, Team Victory, and first
  valid player Pokemon restored to 1 HP/status cured.

### Replay and invariants

For every representative scenario, replay the same:

- Frozen catalog/settings/setup.
- Decision sequence.
- External stat refresh sequence.
- RNG sequence.

Require semantic equality of typed events, RNG trace, final snapshot, outcome,
capture results, and item-consumption facts. Also require byte-for-byte equality
of C01's versioned canonical `FBattleReplayRecord` serialization; never compare
raw in-memory struct bytes.

Global invariants:

- HP, PP, stage, duration, layers, and item counts remain in bounds.
- Permanent calculated stats are never mutated by transient modifiers.
- One accepted action executes at most once.
- Fainted/captured/removed battlers cannot act.
- Invalid/stale/canceled-before-execution paths consume only what their frozen
  rule explicitly permits.
- Terminal state is immutable.
- Hidden information never appears in unauthorized snapshots/events.

## C11B — Build, Automation, and Evidence Gate

Before running Unreal:

- Record source, test, JSON, Data Table/catalog summary, `.uproject`, and
  `DefaultEngine.ini` hashes.
- Confirm the intended test binary was rebuilt from current source.
- Choose unique timestamped report/log paths.

Verification order:

1. Run the most focused failing subsystem prefix during development.
2. Run each completed package prefix.
3. Build `PokemonSolarusEditor Win64 Development`.
4. Run the complete `PokemonSolarus.Battle` Automation prefix.
5. Inspect exported `index.json`, not only process exit code or console text.
6. Classify test failures/warnings/not-run separately from unrelated optional
   engine/plugin startup warnings.
7. Compare all protected hashes after Unreal.

Completion requires:

- Zero failed tests.
- Zero not-run tests.
- Zero test warnings; unrelated engine warnings are documented separately.
- Successful Editor build from the live source.
- No stale generated source/object provides an absent implementation.
- No unapproved `.uproject` or configuration change.
- Evidence paths and source/data hashes recorded in this plan and the index.

Do not delete old reports. Do not remove or rewrite the existing Android File
Server block without explicit approval. If Unreal changes protected files, stop
and ask before any cleanup.

## Final Review

Perform an independent read-only review against:

- Every approved scope item in `00-roadmap-index.md`.
- Every package acceptance criterion.
- The B00B modern-rules snapshot and explicit Solarus exceptions.
- The complete canonical content inventory.
- The public API boundary: no UI, progression, persistence, strategic AI, or
  World/Actor dependencies in core rules.

The reviewer must report missing mechanics, untested branches, stale evidence,
hidden coupling, nondeterminism, content-specific branches, and scope creep.

## Definition of Complete

The reusable Global Battle core is complete only when:

- B00 through C10 are complete with linked evidence.
- C11A's full matrix passes deterministically.
- C11B's build/full-suite/evidence gate passes.
- The independent review has no unresolved Critical or High findings.
- Remaining omissions are explicitly outside the approved bounded catalog, not
  silently unimplemented parts of an approved mechanic.
