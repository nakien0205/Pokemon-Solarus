# C06 — Parties, Switching, and Replacements

Priority: P1  
Status: Blocked by C05C  
Required order: C06A, then C06B

## Objective

Implement voluntary, forced, pivot, and faint-driven movement between stable
party slots and active positions. Preserve correct action costs and Double
Battle replacement timing.

## C06A — Party Legality and Voluntary Switching

Party rules:

- Maximum six party slots per Trainer.
- Eggs cannot enter battle.
- A switch target must be a living reserve owned by the acting Trainer.
- An active battler, fainted battler, captured/removed battler, or already
  reserved switch target is illegal.
- Two active allies cannot choose the same reserve during one selection phase.

Voluntary switch:

- Costs the acting Pokemon's action.
- Participates in B00B's canonical command priority/order.
- Emits action start, left slot, transient cleanup, entered slot, entry trigger,
  and completion events in exact order.
- Uses the battler ID to follow the Pokemon and the active-slot ID to identify
  the battlefield position.

Hooks:

- Switch prevention from trapping and encounter policy.
- Forced switching such as Roar.
- Pivot switching such as U-turn.
- Baton-pass-like state transfer is not proof content; its hook remains typed
  but unpopulated until separately approved.
- Entry hazards and switching Abilities/items attach through C07/C08 trigger
  points rather than bespoke calls.

## C06B — Faint Replacement and Shift/Set

Replacement rules:

- Complete remaining queued actions that Solarus requires before requesting a
  mandatory replacement.
- A mandatory replacement consumes no action on the next normal turn.
- If one reserve must fill an empty side with both slots empty, it enters the
  left slot.
- Reject selecting the same reserve for two empty positions.
- If no living reserve exists, leave the position empty and continue or end
  according to the outcome rules.

Shift/Set:

- Shift is available only in eligible Casual Single Trainer battles.
- Shift is the default battle style when an eligible Casual Single Trainer
  setup does not explicitly choose a style.
- The Shift opportunity occurs at the B00B checkpoint and is a separate typed
  decision that costs no normal-turn action.
- Declining Shift follows Set behavior.
- Wild, Double, and partner Double Battles use Set.
- Encounter configuration may force Set; it may not enable Shift in unsupported
  formats.
- Setup validation normalizes every Double, partner, wild, and otherwise
  ineligible format to Set and never produces a Shift request there.

Transient cleanup:

- Clear or retain stat stages, volatiles, status, Ability state, and held-item
  state exactly as B00B/C07/C08 specify.
- Major status normally persists across switching; faint cleanup is separate.
- Original held-item ownership never changes because of slot movement.

## Tests

C06A:

- Valid switch, active/fainted/egg/wrong-owner/duplicate reserve rejection.
- Two allied switches with distinct reserves and deterministic order.
- Trapped switch rejection, forced switch, pivot switch, and no legal reserve.
- Exact exit/entry event and trigger order.

C06B:

- Single faint replacement and no next-turn action cost.
- Double one-faint/two-faint cases, remaining-action completion, two reserve
  selections, duplicate rejection, and lone-reserve left-slot fallback.
- Shift offered, accepted, declined, and unavailable in every unsupported mode.
- Outcome when one or both sides have no living reserves.
- Deterministic replay and no duplicated entry triggers.

## Acceptance

- Party and active-slot invariants hold after every switch/replacement path.
- Voluntary switches consume exactly one action; mandatory replacements do not.
- Trigger points are emitted for C07/C08 without requiring those packages to
  mutate party arrays directly.
- C07 can add trapping, cleanup, hazards, and entry effects through frozen hooks.
