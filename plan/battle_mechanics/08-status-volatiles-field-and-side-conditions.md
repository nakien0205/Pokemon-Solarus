# C07 — Status, Volatiles, Field, and Side Conditions

Priority: P1/P2  
Status: Blocked by C06B  
Required order: C07A; C07B/C; then C07D

## Objective

Build one deterministic trigger/duration framework, then implement all approved
major statuses, volatile proof conditions, and standard field/side families.
No condition may create its own independent timing loop.

## C07A — Trigger, Duration, and Condition Framework

Freeze trigger points for:

- Battle/turn start, selection eligibility, action-order calculation, before
  action, before accuracy, before hit, before damage, after damage, after hit,
  after action, switch out, switch in, faint, removal, end turn, and expiry.

Each registered trigger has:

- Trigger phase, priority/order key from B00B, owner/source/targets, duration
  owner, remaining turns, layer count, public visibility, and reentrancy token.
- A typed condition/effect ID and immutable definition payload.
- A deterministic creation ordinal used only after canonical ordering keys tie.

Rules:

- Mutations requested by a trigger enter a queue; do not recurse directly into
  the same trigger phase.
- A trigger cannot execute twice for one event unless explicitly marked
  repeatable.
- Simultaneous effects share a group but still have deterministic individual
  event ordinals.
- Start/end/duration events contain facts only, not messages or animation time.
- Switch, faint, capture, battle end, and suppression cleanup use typed policies
  from the definition.

## C07B — Major Status

Implement:

- Burn.
- Paralysis.
- Sleep.
- Freeze, not Frostbite.
- Poison.
- Badly Poisoned with its Toxic counter.

For each status, implement application, immunity, mutually exclusive failure,
action gate, stat/damage modifier, duration/counter, end-turn effect, cure,
switch behavior, faint cleanup, Ability/item hooks, and ordered events exactly
as B00B specifies.

Solarus has no overworld poison or other status damage. Battle status events do
not modify persistent records directly.

## C07C — Approved Volatile Proof Set

Implement:

- Confusion: application, duration, action gate, self-hit calculation, cure.
- Flinch: action denial and end-of-turn cleanup.
- Protect: success/failure chain, blocked effects, consecutive-use state.
- Leech Seed: application immunity, end-turn damage/heal, source changes.
- Partial trapping/bind: residual damage, duration, switch prevention.
- Switch-prevention trapping: application and switch legality.
- Taunt: Status-move legality and duration.
- Encore: locked move, invalid/zero-PP termination.
- Disable: disabled move, duration, and interaction with Encore/Struggle.
- Substitute: HP cost, damage absorption, effect blocking, break event.
- Charging: first-turn setup and second-turn execution/cancellation.
- Recharge: next-action denial and cleanup.
- Semi-invulnerability: Fly-style state, reachability, return, and official
  exceptions represented by move flags.

Official coexistence and conflict rules come from B00B. Unsupported volatile
content uses the framework later; do not add infatuation, Perish Song, Yawn,
Torment, Destiny Bond, or unrelated canonical conditions now.

## C07D — Approved Field and Side Content

Weather:

- Harsh Sunlight, Rain, Sandstorm, Snow.

Terrain:

- Electric, Grassy, Misty, Psychic.

Entry hazards:

- Spikes, Toxic Spikes, Stealth Rock, Sticky Web.

Screens:

- Reflect, Light Screen, Aurora Veil.

Rooms:

- Trick Room, Magic Room, Wonder Room.

Side conditions:

- Tailwind, Safeguard, Mist.

For every entry implement scope, source/owner, activation requirements,
replacement/coexistence, duration, extension hook, layers, active effects,
switch-in effects, removal, expiry, and public snapshot data. Use the trigger
framework for damage/order/status/item interactions.

Gravity, pledge fields, Water Sport, Mud Sport, and other uncommon global/side
effects are framework-only and excluded from this proof set.

## Safe Session Split

- C07A must run alone and freeze shared timing contracts.
- C07B and C07C can be implemented in separate isolated lanes with disjoint
  condition files and tests; one later session integrates their shared action
  gates.
- After C07A, weather and terrain/side definitions may be separate isolated
  content lanes. Entry/switch/damage integration remains serialized.

## Tests

- Every trigger point, order tie, duration decrement, expiry, switch/faint
  cleanup, simultaneous group, and recursion guard.
- Every major-status success, immunity/failure, action gate, modifier, cure,
  residual effect, and coexistence rejection.
- Every selected volatile's activation, blocked path, duration/cleanup, and
  cross-interaction, especially Protect, Substitute, Encore/Disable, trapping,
  charge/recharge, and semi-invulnerability.
- Weather/terrain replacement and expiry; Sandstorm/Grassy residual effects;
  screen damage; Trick Room ordering; Magic/Wonder Room behavior.
- Hazard layers, grounded/type/Ability/item checks, removal, and switch-in faint
  before action.
- Tailwind/Quick Claw/Paralysis/order interactions and Safeguard/Mist blocks.
- No permanent-stat mutation and exact deterministic event/RNG traces.

## Acceptance

- All conditions use the shared trigger framework.
- No recursive double-triggering or nondeterministic iteration remains.
- Battle snapshots contain exact remaining duration/layers for future Battle
  Info.
- C08 can register Ability/item hooks without editing individual conditions.

