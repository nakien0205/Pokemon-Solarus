# C08 — Abilities, Held Items, and Battle Items

Priority: P2  
Status: Blocked by C07D  
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

Ordinary enemy Trainers cannot use Revive. A boss may use it only when encounter
configuration explicitly allows it. Partner Trainers cannot capture.

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
  prevented legal effect, boss Revive policy, and no persistent write.

## Acceptance

- No approved Ability/item requires a bespoke engine branch keyed by species.
- Trigger/reveal ordering is deterministic and does not leak hidden data.
- Consumption and ownership facts are sufficient for a future inventory service
  without core owning that service.
- C09 can implement capture/partner flows using the frozen Bag and ownership
  contracts.
