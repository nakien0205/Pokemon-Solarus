# Four-Move Battle Selection

> **Status**: Accepted
> **Author**: User and Codex
> **Last Updated**: 2026-09-03
> **Implements Pillar**: One Good Battle First; Familiar Strategy, Explicit Solarus Rules; Readability Before Spectacle; Small Now, Reusable Later

## Overview

Four-Move Battle Selection replaces the showcase's automatic one-move Fight
action with an active choice. The selector has four move slots, but a Pokemon
may have fewer than four assigned moves. The player identifies which assigned
moves are currently usable and commits one legal move for the turn. A temporary
opponent policy independently makes one uniform choice from its own legal
moves. Without this system, the battle loses active move selection and returns
to an automatic action. The temporary loadout proves selector mechanics,
decision shapes, and target handling; it does not claim to provide meaningful
strategic balance. Broader strategic trade-offs are deferred to Move Pools and
Special Effects.

## Player Fantasy

Choosing a move should feel like a deliberate commitment under pressure. The
player weighs their Pokémon’s available moves without knowing which move the
opponent will choose, then confirms one choice for the turn and accepts its
consequences. Each decision should feel important while remaining clear and
fair. Pokémon Ultra Sun is the broad feeling reference; specific presentation
and interaction details remain for the user-led UI design.

This supports the pillar: “Battles should feel recognizably like modern Pokémon
while documenting every intentional Solarus exception.”

## Owner-Approved Review Boundaries

- The user has approved and exclusively owns all UI/UX design, visual design,
  art, layout, styling, assets, composition, motion appearance, audio treatment,
  and related presentation decisions for this system.
- Those owner decisions are outside agent review. Agents must not reopen,
  question, assess, redesign, or modify them, and must not read user-owned
  UI/UX or art reference material merely to review it.
- Agents must not call art, UX-design, UI-design, audio-design, technical-art,
  or other presentation-design subagents for this system. Agents may read only
  the functional code-behind contracts needed to implement or validate Battle
  mechanics, state, input routing, data, adapters, and automated tests.
- The current keyboard mapping is final for this milestone: Arrow keys navigate,
  `C` is Confirm, `X` is Cancel, and `V` is Battle Info. Throughout this
  document, a **tap** means pressing and releasing `C` before `T_hold`; a
  **hold** means keeping that same `C` press active until `T_hold`. Agents must
  not reopen or ask review questions about these button decisions.
- The opponent-policy candidate count is hard-capped to `1–4`. This limit is a
  closed owner decision and must not be reopened or questioned in later review.
- Battle Info's `V` mapping and open/close behavior require user manual
  verification only. This GDD requires no automated test for that key binding.

## Detailed Design

### Core Rules

1. This step uses the following temporary fixed loadout. It proves selection
   from a four-slot capacity and the required decision shapes. It is not the
   final showcase move set and is not a strategic-balance test:

| Pokémon | Slot | Move | Eligibility |
|---|---:|---|---|
| Charizard | 1 | Swift | TM |
| Charizard | 2 | Earthquake | TM |
| Charizard | 3–4 | Empty | Not applicable |
| Venusaur | 1 | Vine Whip | Level-up |
| Venusaur | 2 | Earthquake | TM |
| Venusaur | 3–4 | Empty | Not applicable |

2. The four assignments reference three distinct ordinary Move IDs: Swift,
   Earthquake, and Vine Whip. All three already have gameplay-catalog records
   and deal direct damage. Separate move presentation data supplies one shared
   localized display name and short description per Move ID. UI code must not
   contain move-name or move-description text.

3. Four-slot selection replaces Charizard's prototype Quick Attack with Swift
   and adds Earthquake as its second choice. The later special-effects step may
   replace Swift with Flamethrower.

4. The showcase configuration requires the exact ordered assignments in rule
   1. Before battle, it validates assigned Move IDs, catalog records, localized
   display names and descriptions, and the direct-damage restriction in rule 2.
   The validator explicitly accepts a valid `bAlwaysHits` damaging move rather
   than treating its numeric accuracy `0` as invalid. Empty slots contain no
   Move ID and require no catalog or presentation record. Duplicate Move IDs are
   rejected only within one Pokemon's non-empty assigned move slots. Different
   Pokemon may reference the same Move ID, so both approved Earthquake
   assignments share one move definition and one presentation record. `MoveId`
   identifies that shared definition, not one equipped copy. Setup identifies
   an equipped assignment by `SourcePokemonId` plus Move Slot Index; current
   Battle state identifies it by `BattlerId` plus that Move Slot Index. Party
   position and active Battle position do not redefine move ownership. This
   showcase rule does not change the general BattleEngine allowance of zero to
   four assigned moves.
   Pokémon eligibility is manually verified because the general learnset
   database is deferred. Rule 1 eligibility was verified on 2026-09-03 against
   Generation 9 entries in Pokemon Showdown commit
   `34caa98811fd6ed5d2f173ec1fc29dd9bd4bc91d`, `data/learnsets.ts`.

5. Selecting **Fight** opens four rectangular move slots. Assigned moves fill
   slots from the top in authored order. Remaining trailing slots stay blank,
   cannot receive focus, cannot open Details, and cannot be confirmed.

6. `BattleEngine` provides current legality, targets, PP, typed effectiveness,
   and unavailable reasons. Its observer-safe snapshot supplies an effectiveness
   summary for every displayed assigned ordinary move. For an unavailable move,
   the engine supplies the summary when it can determine a valid current target
   set independently of that move's availability restriction; otherwise it
   supplies typed `Unknown`. The UI must not calculate, infer, or change any of
   these facts.

7. An unavailable move remains focusable and readable. Confirming it shows its
   reason but submits nothing and consumes nothing.

8. Cancel from Move Selection returns to the command menu without submitting a
   decision or changing battle state. Cancel behavior inside Details and Battle
   Info follows their rules below.

9. Confirming a legal move submits one of the two decision shapes already
   exposed by the current request. Vine Whip submits the sole opponent as its
   explicit target. Swift and Earthquake submit no target ID; `BattleEngine`
   resolves their fixed target sets. Singles shows no separate target-selection
   screen for either shape. When built-in Struggle is exposed, it uses the
   explicit-target shape and submits the sole opponent.

10. Once accepted, the player cannot change the move. The opponent-policy input
    excludes the player's submitted decision, so the opponent cannot inspect
    the player's choice before resolution.

11. After the player's decision is accepted, `BattleEngine` creates the sole
    opponent's current Singles request and a separate ordered complete-candidate
    policy view. For ordinary equipped moves, the engine iterates the acting
    battler's non-empty move slots by ascending Move Slot Index. An automatically
    targeted legal move contributes one no-target Fight decision. An explicitly
    targeted legal move contributes one Fight decision per legal target, in the
    target resolver's stable active-slot order. Existing lexically canonical
    request arrays remain legality data and do not define candidate order. Each
    ordinary private candidate retains its source Move Slot Index for validation
    and test evidence.

    Built-in Struggle is the sole exception to the equipped-slot path. When the
    request exposes Struggle as its only legal move, the engine creates exactly
    one system candidate, `C[0]`, containing Struggle and the sole opponent as
    its explicit Singles target. That candidate has no source Move Slot Index;
    selector slot 1 is presentation placement and must never be treated as an
    equipped move slot. The ordinary slot-order proof excludes this system
    candidate.

    The temporary opponent policy selects only from the completed ordered list;
    it never reorders candidates or constructs or changes a move or target ID.
    The engine candidate builder has a hard capacity of four complete decisions.
    It must reject an attempt to add a fifth candidate before it constructs the
    policy input. It never truncates, clamps, merges, or silently discards a
    candidate.

12. `BattleEngine` must expose one reusable opponent-only atomic policy
    operation. A separate ADR must be approved before this operation is
    implemented. The ADR owns the public API, decision-selection RNG
    transaction, staging, final identity check, rollback, retry, trace, and
    replay-publication design. The following gameplay requirements constrain
    that ADR:

    - The input identifies the exact battle, state version, decision owner,
      acting battler, active slot, engine-created opponent request, and matching
      observer-filtered opponent snapshot.
    - It exposes no core snapshot, private engine state, mutation API, RNG API,
      or accepted player decision.
    - The current uniform policy ignores the observation and uses only the
      ordered legal decisions. A later strategic policy may use only public and
      revealed facts in the same filtered observation.

    - The operation owns selection RNG, validation, final request-identity
      revalidation, opponent decision acceptance, action-queue locking, trace,
      and publication of its immediate decision-selection and decision-
      acceptance events, resolution records, and replay records.
    - Selection RNG uses a rollback-capable decision-selection transaction. It
      does not pretend that the existing action-scoped transaction already has
      an action identity before the turn locks.
    - With one legal decision, the operation stages no RNG draw. With two to
      four, it stages exactly one semantic uniform draw.
    - It publishes the opponent decision, RNG progress, trace, immediate
      decision-selection and decision-acceptance records, and the locked action
      queue together only after the final identity check succeeds.
    - Stale, rejected, or failed work publishes none of those values.
    - If the first operation becomes stale and one fresh operation succeeds,
      the stale operation publishes nothing and the fresh operation publishes
      exactly its own decision, RNG progress, trace, immediate decision records,
      replay records, and queue lock once. No value staged by the stale operation
      survives.
    - Atomicity covers only the opponent operation. The player's already
      accepted decision remains committed if opponent selection fails.
    - The operation ends when the selected opponent decision is accepted, the
      accepted decision sequence locks the action queue, and those immediate
      records are published. Resolving later locked actions is outside this
      operation. Each later action remains a separate checkpoint under ADR-0002;
      a later action failure does not roll back the completed opponent operation,
      either accepted decision, or the queue lock.
    - This milestone delivers exactly one opponent request in Singles. Later
      multi-request policy work may extend the boundary without changing these
      privacy, validation, and transaction rules.

13. The player-then-opponent decision order remains authoritative. After the
    player's choice is accepted, `BattleEngine` creates the real opponent
    request. If no ordinary move is legal, it supplies Struggle, so a valid
    Singles opponent request always produces at least one legal Fight decision.
    A policy candidate count outside `1–4` is an internal validation failure,
    not a normal battle outcome. The hard-bounded policy input cannot represent
    more than four candidates; an attempted fifth candidate fails during
    construction. An empty or over-capacity construction consumes no RNG,
    commits no opponent work, and triggers no retry. The runtime halts automatic
    advancement and exposes `InvalidOpponentCandidateCount` to presentation.
    The player's accepted decision remains committed. The engine does not build
    a speculative opponent request before accepting the player.

14. When all assigned moves are unusable:

    - Slot 1 shows Struggle, matching the approved Battle HUD rule. Slots 2–4
      stay blank.
    - Struggle is automatically focused as the only selectable legal action.
    - The player must confirm Struggle.
    - The opponent selects Struggle when it is its only legal move.
    - The explanation for this state comes from battle presentation data; the
      UI does not infer it.
    - Holding Confirm on Struggle opens the same persistent Move Details panel
      used by ordinary moves. The panel receives Physical Category, Power `50`,
      and typed `AlwaysHits` Accuracy from the engine-owned built-in Struggle
      definition, plus the localized name and short description from the
      separate Struggle system presentation. It does not request an ordinary
      move-presentation record or infer these values in UI code.
    - Because Struggle is the only displayed action, Up and Down in its Details
      panel leave focus on Struggle. A tap submits Struggle; a hold or Cancel
      closes Details by the ordinary Details rules.
    - Struggle's visual treatment remains user-owned.

15. The opponent operation locks the action queue after both decisions are
    accepted. `BattleEngine` then resolves each locked action through its
    existing ADR-0002 checkpoints. Those later checkpoints are not part of the
    opponent operation's rollback boundary.

16. This step does not add a general learnset database, strategic AI, new
    special-effect behavior, or final move-menu visuals.

### Move Details and Battle Info

1. `T_hold` is the positive Confirm-hold threshold chosen by the user before
   selector input implementation and validation. No numeric default is approved
   by this document.
2. Pressing `C` (Confirm) captures the current request identity, focused Move
   ID, and selector mode. Releasing `C` before `T_hold` is a tap. Keeping that
   same press active until `T_hold` is a hold and triggers exactly once.
3. During a hold that can open or close Move Details, code-behind exposes
   normalized progress `H` from `0` to `1` for a user-designed circular loading
   cue. The cue's appearance remains user-owned.
4. A tap in Move Selection submits the captured legal move. A tap on an
   unavailable move shows its engine-provided reason and submits nothing.
5. A hold in Move Selection opens a persistent details panel. Releasing the
   originating hold leaves the panel open and submits nothing.
6. While the panel is open for ordinary assigned moves, Up and Down change
   focus among those moves and update the same panel. Blank slots are ignored.
   The panel shows Category, Power, typed Accuracy, and a short description
   from validated presentation data.
7. When Struggle is the sole displayed action, its Details panel uses the
   engine-owned built-in definition and separate localized system presentation
   defined by Core Rule 14. Up and Down leave focus unchanged.
8. A tap in Move Details submits the captured legal move on release. A tap on
   an unavailable move shows its reason, leaves Details open, and submits
   nothing.
9. A hold in Move Details closes the panel when `T_hold` is reached. Releasing
   that hold submits nothing. Pressing Cancel closes Details immediately,
   without displaying or waiting for the circular loading cue.
10. Key-repeat events do not create another tap or hold. Changing focus, a stale
   request, Battle Info opening, Cancel, input cancellation, or application
   focus loss cancels the pending Confirm gesture and resets `H`.
11. `V` opens read-only Battle Info from Move Selection or Move Details. It
    freezes selector input and records the prior selector mode, exact request,
    and focused Move ID. `V` or Cancel closes Battle Info and restores them when
    the request is still current.
12. If the request becomes stale while Details or Battle Info is open, the
    overlay closes and the selector refreshes. Focus stays on the same Move ID
    when it still exists. Otherwise, focus moves to the first legal move in
    authored slot order, or to Struggle when Struggle is the sole legal action.
13. The runtime selector-session owner processes refresh in this order:
    invalidate old input, cancel any pending Confirm gesture, close the overlay,
    apply the new presentation state, normalize focus, then enable input.

### Move List Navigation

1. The prototype selector is one rectangular container holding four
   well-spaced rectangular move slots in one vertical list. Assigned moves fill
   from the top and unused trailing slots stay blank. Exact dimensions, spacing
   values, styling, and assets remain user-owned.
2. The first legal move in authored slot order receives initial focus.
3. Only Up and Down change focus. They traverse assigned moves, including
   unavailable assigned moves, and skip every blank slot. Up on the top assigned
   move and Down on the bottom assigned move keep focus in place; navigation
   never wraps.
4. Unavailable authored moves remain in the list and remain reachable. Blank
   slots contain no action, text, unavailable reason, focus, or Details state.
5. Move Details uses the same vertical order. Battle Info restores the prior
   focused Move ID when its request remains current.

### States and Transitions

| State | Entry | Player action | Result |
|---|---|---|---|
| Command selection | A player decision is required | Select Fight | Open move selection |
| Move selection | Fight was selected | Up or Down | Move focus among assigned moves within the non-wrapping vertical list; blank slots are skipped |
| Move selection | Any move focused | Press `C` (Confirm) | Capture the request and move; begin hold progress |
| Move selection | Confirm released before `T_hold` | Tap | Submit a legal move, or show an unavailable reason |
| Move selection | Confirm reaches `T_hold` | Hold | Open persistent Details without submitting |
| Move details | Details are open | Up or Down | Change focus and update the same panel |
| Move details | Confirm released before `T_hold` | Tap | Submit a legal move, or show an unavailable reason and stay open |
| Move details | Confirm reaches `T_hold` | Hold | Close Details without submitting |
| Move details | Details are open | Cancel | Close Details immediately without the loading cue |
| Move selection or details | Current request exists | Press `V` | Open Battle Info and freeze selector input |
| Battle Info | Request remains current | `V` or Cancel | Restore the prior selector mode and focused Move ID |
| Move selection | Any move focused | Cancel | Return to command selection without submitting |
| Struggle selection | Confirm released before `T_hold` | Tap | Submit Struggle from slot 1; slots 2–4 stay blank |
| Struggle selection | Confirm reaches `T_hold` | Hold | Open persistent Struggle Details without submitting |
| Choice accepted | Player move was accepted | None | Run the opponent-only atomic policy operation |
| Policy refresh | First policy operation became stale | None | Roll back and allow at most one fresh operation against the current request |
| Policy error | A fresh retry is stale or another policy failure occurs | None | Halt automatic advancement and show a typed error |
| Turn resolution | Both choices were accepted | None | Resolve through `BattleEngine` |
| Refreshed selection | Request became stale | None | Cancel input, close overlays, present the current request, and normalize focus |

### Interactions with Other Systems

| System | Responsibility |
|---|---|
| Showcase configuration | Assigns the approved two ordered moves to each Pokémon within the four-slot capacity |
| Catalog | Supplies each move's validated battle mechanics |
| BattleEngine | Owns legality, RNG, targets, acceptance, resolution, events, and replay |
| Move presentation resolver | Supplies immutable localized move names and short descriptions keyed by Move ID |
| Player move selector | Displays validated presentation facts, owns local focus and Confirm progress, and submits player intent |
| Temporary opponent policy | Selects one complete legal Fight decision through the engine-owned atomic policy operation |
| Battle presentation | Displays results without changing battle state |
| Move Pools, step 5 | Reuses this selector with broader Pokémon move assignments |
| Special Effects, step 6 | May replace Swift with Flamethrower |
| Strategic AI, step 7 | Replaces the temporary uniform policy |

### Move Presentation Data Contract

Before this logical contract is implemented, a separate approved presentation-
composition ADR must amend or supersede the relevant part of ADR-0001. That ADR
owns the physical Unreal storage choice, composition-root reference, cooked
reachability, runtime-bundle extension, resolver API, and atomic loading rules
for both ordinary move presentation and the separate localized Struggle system
presentation.

The same ADR owns the observer-safe `BattleEngine` projection extension needed
to supply typed effectiveness for every displayed assigned ordinary move,
including legal automatically targeted moves and unavailable moves when a valid
current target set can be determined independently of the availability
restriction. It must define the target-feasibility source, privacy boundary,
stable move identity, failure behavior, serialization and replay compatibility,
and any required schema or consumer migration. The logical content and
validation requirements below constrain that ADR; they do not prescribe a Data
Table layout, resolver interface name, or other physical implementation. This
GDD alone does not authorize changing ADR-0001, the runtime bundle, the snapshot
contract, or replay serialization.

1. The ordinary move-presentation source selected by the ADR provides exactly
   one immutable logical record per distinct required ordinary Move ID.
   Different Pokemon that use the same Move ID share that record. The current
   showcase therefore requires three ordinary records: Swift, Earthquake, and
   Vine Whip. Each record owns a non-empty localized display name and short
   description. Blank slots and the separate Struggle system presentation have
   no ordinary move-presentation record.
2. Runtime composition validates the approved presentation records and exposes
   them through the immutable read-only resolver contract selected by the ADR.
   Gameplay move definitions remain free of localized UI text.
3. The presentation adapter combines, by Move ID:

    - Authored assigned-move order from the active Pokémon.
   - Type, Category, Power, `bAlwaysHits`, numeric Accuracy, and base PP rules
     from the validated gameplay catalog.
   - Current and maximum PP plus typed effectiveness from the observer-safe
     snapshot. This includes unavailable assigned moves by Core Rule 6.
   - Legal state or exactly one typed unavailable reason from the current
     request.
   - Localized name and short description from the move presentation resolver.

4. Accuracy is a typed display value. A valid `bAlwaysHits` move produces
   `AlwaysHits`; an ordinary accurate move produces `NumericPercent` with a
   value from `1` through `100`. The UI never treats numeric `0` as a percentage
   or infers always-hit behavior itself.
5. A displayed ordinary move fails closed when any required source is missing,
   duplicated, empty, invalid, or contradictory. No partial or invented move
   tile reaches the UI.
6. Struggle remains an explicit engine-built system case: localized system
   presentation, typeless styling, no regular catalog row, PP dash, and
   visibility only when the current request exposes it. Its Details payload
   combines the built-in Struggle definition's Physical Category, Power `50`,
   and `bAlwaysHits` value with the localized system name and short description.
   The adapter converts that accuracy to typed `AlwaysHits` and never invents
   an ordinary move-presentation record.

### Selector Presentation Failure Contract

1. The presentation adapter builds and validates one complete selector or
   Details projection before applying any part of it. Missing, duplicated,
   empty, invalid, or contradictory required data rejects the whole candidate
   projection and produces the one matching code from the Typed Error Contract.
2. On rejection, the selector-session owner cancels the pending Confirm
   gesture, resets `H`, closes Details or Battle Info, and disables both
   selector and command input. It makes no selector-to-engine call and changes
   no Battle state, PP, RNG, trace, or replay value.
3. If a valid Battle display was applied previously, its last valid base HUD
   and any previously applied selector presentation remain visible and
   unchanged; no partial candidate projection replaces them. If no valid Battle
   display has ever been applied, the HUD and selector remain hidden. In both
   cases, presentation exposes the specific code and localized text through the
   runtime error path.
4. Input remains disabled until the normal accepted-update path supplies a
   complete current projection that validates. A replaced projection never
   restores a cancelled gesture or accepts input captured for the failed one.

### Typed Error Contract

Every selector, move-presentation, and opponent-policy failure exposes exactly
one stable error code and its specific localized text. Presentation receives
the code and text; diagnostics may additionally receive the involved Move ID,
source Pokémon ID, slot index, request identity, and underlying rejection. A
generic `Battle error` message must not replace a more specific reason below.
The user exclusively owns the error surface's visual treatment.

When multiple triggers are detected within one validation boundary, emit only
the first matching code in the applicable precedence list:

1. Showcase and presentation validation: `DuplicateAssignedMove`,
   `ShowcaseAssignmentsMismatch`, `MissingMoveDefinition`,
   `MissingMovePresentation`, `EmptyMovePresentationText`,
   `UnsupportedMoveEffect`, `InvalidMoveAccuracy`, then
   `ContradictoryMoveData` as the catch-all.
2. Opponent candidate and policy validation: `InvalidOpponentCandidateCount`,
   `OpponentPolicyWrongOwner`, `OpponentPolicyIllegalTarget`,
   `OpponentPolicyStale`, `OpponentPolicyRandomFailure`,
   `OpponentPolicyRejected`, then `OpponentPolicyInternalFailure` as the
   catch-all.
3. `InvalidHoldThreshold` belongs to hold-configuration validation and is
   emitted before selector input can activate.

A terminal failure at an earlier operation stage prevents later stages from
running; precedence resolves overlapping triggers discovered at the same stage
and does not evaluate hypothetical later failures.

| Error code | Trigger | Required visible text |
|---|---|---|
| `ShowcaseAssignmentsMismatch` | The showcase assignments differ from Core Rule 1 | `The showcase move assignments do not match the approved configuration.` |
| `DuplicateAssignedMove` | One Pokémon has the same Move ID in more than one non-empty slot | `This Pokémon has the same move in more than one slot: {MoveId}.` |
| `MissingMoveDefinition` | An assigned ordinary Move ID has no gameplay record | `A required move definition is missing: {MoveId}.` |
| `MissingMovePresentation` | An assigned ordinary Move ID has no presentation record | `Move presentation data is missing: {MoveId}.` |
| `EmptyMovePresentationText` | A required localized name or description is empty | `A move name or description is empty: {MoveId}.` |
| `UnsupportedMoveEffect` | An assigned move contains behavior outside this milestone | `This move uses an unsupported effect: {MoveId}.` |
| `InvalidMoveAccuracy` | Ordinary accuracy is outside `1–100`, or `bAlwaysHits` data is contradictory | `This move has invalid accuracy data: {MoveId}.` |
| `ContradictoryMoveData` | Catalog, snapshot, request, or presentation facts disagree | `Move information does not match the current battle data: {MoveId}.` |
| `InvalidHoldThreshold` | `T_hold` is missing, non-finite, or not greater than zero | `Move Details hold time must be greater than zero.` |
| `InvalidOpponentCandidateCount` | Candidate construction produces fewer than one or attempts more than four decisions | `Opponent legal-choice count must be between 1 and 4; received {N}.` |
| `OpponentPolicyWrongOwner` | The policy decision owner does not match the request | `The opponent decision belongs to the wrong Trainer.` |
| `OpponentPolicyIllegalTarget` | The policy returns a target not allowed by the request | `The opponent selected an illegal target.` |
| `OpponentPolicyRejected` | Final BattleEngine validation rejects the decision | `The opponent decision was rejected: {Reason}.` |
| `OpponentPolicyRandomFailure` | The semantic uniform draw cannot be completed or committed | `Opponent move-selection RNG failed.` |
| `OpponentPolicyStale` | The operation's request identity is no longer current | `The opponent decision request changed before selection completed.` |
| `OpponentPolicyInternalFailure` | Another policy invariant or internal operation fails | `Opponent move selection failed internally.` |

- A selector or presentation error clears only after a complete valid current
  projection applies through the normal accepted-update path.
- A policy error that halts automatic advancement remains exposed for that
  halted operation. No automatic fallback or generic replacement hides it.
- Localization may translate the required text without changing its specific
  meaning, error code, placeholders, or trigger.

## Formulas

### Opponent Uniform Selection

The `opponent_uniform_selection` formula is defined as:

For `N = 1`:

`selected_decision = C[0]; P(selected_decision = C[0]) = 1`

For `2 <= N <= 4`:

`D = UniformInteger(0, N - 1); selected_decision = C[D]; P(selected_decision = C[i]) = 1 / N`

**Variables:**

| Variable | Symbol | Type | Range | Description |
|---|---|---|---|---|
| Legal decisions | `C` | Hard-bounded ordered complete Fight-decision list | Exactly 1–4 entries | Separate engine-built policy view validated against the exact opponent request. Ordinary candidates are ordered by source Move Slot Index, then stable active-target order. Sole built-in Struggle is `C[0]`, has an explicit sole-opponent target, and has no source Move Slot Index. The type cannot hold a fifth entry |
| Legal decision count | `N` | Integer | 1–4 | Number of entries in `C` |
| Drawn index | `D` | Integer | 0 to `N - 1` | One replayable uniform draw when `N` is 2–4; absent when `N` is 1 |
| Candidate index | `i` | Integer | 0 to `N - 1` | Index of any legal candidate |
| Selection probability | `P` | Fraction | 1/4 to 1 | Chance that candidate `i` is selected |

**Output Range:** One complete legal decision from `C`. Each candidate has a
25%–100% chance depending on `N`.

**Example:** If `C` contains four complete decisions, the draw is `0–3`, and
each decision has a 25% chance.

- Preserve the complete-candidate policy-view order supplied by `BattleEngine`;
  the lexically canonical request arrays do not define this order.
- Do not filter or reorder it again.
- With Struggle as the only legal decision, use the engine-built system
  candidate `C[0]`, with its explicit sole-opponent target and no source Move
  Slot Index, and select it without an RNG draw; probability is 100%.
- Candidate construction must succeed with exactly `1–4` entries before the
  policy operation or RNG transaction can begin.
- Reject `N = 0` as `InvalidOpponentCandidateCount`. Reject an attempt to add a
  fifth candidate with the same code before a policy input exists. Consume no
  RNG, commit no opponent work, do not retry, and never clamp or truncate.
- Stale, rejected, or failed selection consumes and records nothing.
- "One draw" means one successful semantic traced draw. Internal rejection
  sampling may advance private raw values before that one semantic result.
- Do not use modulo selection, invalid-slot retries, or a fallback formula.
- Existing damage, accuracy, PP, effectiveness, and turn-order formulas are
  reused unchanged.

### Confirm Hold Progress

The `confirm_hold_progress` formula is:

`H = Clamp(t / T_hold, 0, 1)`

| Variable | Symbol | Type | Range | Description |
|---|---|---|---|---|
| Held time | `t` | Seconds | `0` or greater | Monotonic time since the accepted Confirm press |
| Hold threshold | `T_hold` | Seconds | Greater than `0` | User-approved threshold required before input implementation |
| Hold progress | `H` | Fraction | `0`–`1` | Code-behind value rendered by the circular loading cue |

- Releasing while `t < T_hold` is a tap.
- Reaching `t >= T_hold` triggers the hold action exactly once.
- The release after a triggered hold performs no action.
- Cancellation resets `t` and `H` without submitting a move.
- Cancel closes Move Details immediately and does not start or display hold
  progress.

## Edge Cases

- **If some moves are unavailable:** Keep them visible, exclude them from
  selection, and show their reasons. Display the engine-provided effectiveness
  summary when a valid current target set can be determined independently of
  the availability restriction; otherwise display typed `Unknown`.
- **If fewer than four moves are assigned:** Fill slots from the top. Keep all
  remaining trailing slots blank and unfocusable.
- **If all assigned moves are unavailable:** Show Struggle in slot 1, keep slots
  2–4 blank, focus Struggle, and require confirmation. Holding Confirm opens
  full Struggle Details from its built-in definition and localized system
  presentation; Up and Down leave its sole focus unchanged.
- **If PP reaches zero after use:** Mark the move unavailable on the next
  request.
- **If the request changes while the menu is open:** Reject old input, consume
  nothing, cancel Confirm progress, close overlays, and refresh in the defined
  lifecycle order.
- **If Confirm repeats while held:** Ignore repeat events and accept at most one
  tap or hold action for the original press.
- **If focus changes while Confirm is held:** Cancel the pending gesture and
  reset the circular progress before moving focus.
- **If the application loses focus while Confirm is held:** Cancel the pending
  gesture. Refocusing cannot submit the earlier press.
- **If Cancel is pressed after acceptance:** Ignore it because the choice is
  committed.
- **If the opponent has no ordinary legal move:** Supply Struggle before the
  opponent policy operation begins, after the player choice is accepted.
- **If the opponent policy receives an empty candidate list:** Treat it as an
  internal invariant failure, consume no RNG, commit no opponent work, do not
  retry, and halt automatic advancement with visible
  `InvalidOpponentCandidateCount`.
- **If candidate construction attempts a fifth decision:** Reject construction
  with `InvalidOpponentCandidateCount` before policy input or RNG exists. Never
  clamp, truncate, merge, or retry the list.
- **If Struggle is the opponent's only legal move:** Select Struggle without an
  RNG draw from the engine-built `C[0]` system candidate. That decision contains
  the sole opponent as its explicit target and has no source Move Slot Index.
- **If the first opponent operation is stale:** Roll it back completely. Start
  at most one distinct fresh operation only when a current opponent request
  still exists and the accepted player decision remains valid. The fresh
  operation may publish its own successful draw and result. If it succeeds, it
  publishes exactly its own decision, RNG progress, trace, immediate decision
  records, replay records, and queue lock once; nothing staged by the stale
  operation survives.
- **If the fresh opponent operation is also stale:** Roll it back, halt with a
  typed visible error, and do not try again.
- **If the opponent no longer needs a decision:** Continue or end the battle
  normally without starting the fresh operation.
- **If opponent policy validation, RNG, or internal work fails:** Roll back,
  halt with its typed visible error, and do not retry or use another policy as
  a fallback.
- **If a later locked action fails:** Handle that action through its separate
  ADR-0002 checkpoint. Do not roll back the completed opponent operation, either
  accepted decision, or the action-queue lock.
- **If a request becomes stale while an overlay is open:** Close the overlay,
  refresh, and normalize focus by the Move Details and Battle Info rules.
- **If a selector or Details projection fails validation:** Reject the entire
  candidate projection, cancel its gesture, close overlays, disable input, and
  expose one typed presentation error. Preserve the last valid base display, or
  keep the HUD hidden when no valid display has ever been applied.

## Dependencies

| System | Direction | Strength | Required connection |
|---|---|---|---|
| BattleEngine and the required opponent-policy ADR | This depends on them | Hard | Both explicit-target and automatically targeted legal decisions; a system Struggle candidate without equipped-slot identity; opponent-filtered observations; the observer-safe snapshot owner extended under the presentation-composition ADR below; decision-selection RNG transaction; opponent-only atomic acceptance and queue locking; typed errors, events, and replay |
| Battle decision requests | This depends on them | Hard | Current versioned legal and unavailable choices |
| Battle catalog and showcase configuration | This depends on them | Hard | Existing mechanical move definitions and the two approved assigned moves per Pokémon |
| Move presentation data and required presentation-composition ADR | This depends on them | Hard | Immutable localized names and short descriptions keyed by Move ID plus separate localized Struggle system presentation; the ADR must amend or supersede ADR-0001 for both paths' storage, runtime composition, cooking, loading, validation, and resolver ownership before implementation; it also owns the observer-safe effectiveness projection for legal automatically targeted and unavailable assigned moves, including target-feasibility, privacy, failure, serialization, replay, schema, and consumer-migration rules |
| Runtime driver | This depends on it | Hard | Opens requests, submits decisions, and advances the battle |
| Battle presentation and HUD data | This depends on them | Hard | Fail-closed selector/details state, observer-safe move information, `AlwaysHits`, unavailable reasons, and Confirm progress |
| Keyboard input routing | This depends on it | Hard for this milestone | Up, Down, `C` Confirm press/release/cancel, `X` Cancel, `V` Battle Info, and application-focus cancellation |
| Command Child Flows, step 2 | Comes after this | Delivery order only | Reuses safe entry and return through the command menu |
| Move Pools, step 5 | Depends on this | Hard for step 5 | Reuses the four-move selector with broader Pokémon content |
| Special Effects, step 6 | Extends this | Soft | Reuses selection while expanding move behavior and may replace Swift |
| Strategic AI, step 7 | Replaces part of this | Hard for step 7 | Replaces only the policy; the reusable engine boundary remains |
| Final move-menu visual treatment | Delivery handoff | Hard for final playable approval | The vertical-list structure is fixed; the user provides dimensions, spacing values, styling, assets, and circular-cue appearance |

The general learnset database is not a dependency of this step.

## Tuning Knobs

This system introduces one input tuning value and no new balance values.

- Move power, accuracy, PP, type, and effects remain owned by the existing
  catalog.
- The showcase configuration owns the two assigned Move IDs per Pokémon.
- `T_hold` is the positive Confirm-hold threshold. Its value is solely a user
  decision and must be supplied before selector input implementation and
  validation.
- The temporary opponent policy is always uniform; it has no weights or
  difficulty setting.
- Strategic AI later replaces the temporary policy, while the engine-owned
  atomic policy boundary remains.

This keeps the selector reusable and prevents duplicate balance settings.

## Visual/Audio Requirements

> **Owner-only record:** The user has approved this system's visual, audio, art,
> and presentation direction. This section is retained only to record the
> owner's requirements. Agents must not review, question, reinterpret, or
> redesign it and must not call presentation-design subagents.

| Event | Required feedback meaning |
|---|---|
| Focus changes | Identify the focused move; no choice has been committed |
| Confirm is held to open or close Details | Show user-designed circular progress driven by `H`; no choice is committed by the hold |
| Move details opened or updated | Show details for the currently focused move without committing it |
| Move details closed by a hold | Return to move selection after the circular progress completes, without committing a move |
| Move details closed by Cancel | Return immediately without showing or waiting for the circular progress |
| Legal move confirmed | The choice was accepted and cannot be changed |
| Move selection cancelled | Returned to command selection without changing battle state |
| Unavailable move confirmed | Explain why it cannot be used and confirm that nothing was submitted or spent |

- The user alone chooses, approves, and implements the visual and audio
  treatment.
- Essential information cannot rely only on sound, colour, or animation.
- Focus must remain visibly identifiable.
- An unavailable reason must remain readable as text.
- This section fixes only the one-column structure and progress meaning. It does
  not decide dimensions, spacing values, colours, assets, circular-cue
  appearance, motion styling, sound files, volume, or mixing. `T_hold` is the
  separate user-owned input value.
- If the user supplies an unavailable-attempt sound, it applies only to move
  selection. Existing top-level command behavior remains unchanged.

## UI Requirements

> **Owner-approved and outside agent review:** The user exclusively owns and
> has approved the UI/UX decisions in this section. Agents may consume the
> functional state, input, and data contract for code-behind work, but must not
> review or redesign the UI/UX or call UI/UX-design subagents.

| Information or action | Requirement |
|---|---|
| Four-slot capacity | Show four well-spaced rectangular slots in one vertical rectangular container; assigned moves fill from the top and unused trailing slots stay blank; when Struggle is sole legal, show it in slot 1 and keep slots 2–4 blank |
| Move data | Show localized name, approved type colour and symbol without a written type name, current/max PP, effectiveness, and availability |
| Initial focus | Focus the first legal move in authored slot order |
| Focus | Clearly identify exactly one focused non-blank option; blank slots never receive focus |
| Unavailable move | Keep it focusable and show its engine-provided reason |
| Struggle | Show it only when supplied as the legal fallback; PP displays as a dash; full Details use the built-in Physical Category, Power `50`, typed `AlwaysHits`, and localized system description |
| Navigation | Up and Down only; traverse assigned moves including unavailable ones, skip blank slots, and stop at the top and bottom without wrapping |
| Confirm | Release before `T_hold` to submit the captured legal move or show its unavailable reason |
| Cancel | Return to Command Selection before acceptance |
| Confirm progress | Expose `H` to a user-designed circular cue for holds that open or close Details; Cancel closes Details immediately without the cue |
| Move details | Reach `T_hold` to open; release keeps it open; Up/Down updates it; reach `T_hold` again or press Cancel to close it |
| Detail content | For ordinary moves, resolve localized name and short description from move presentation data and combine them with catalog Category, Power, and typed numeric or `AlwaysHits` accuracy; for Struggle, combine its localized system presentation with its engine-owned built-in definition |
| Battle Info | `V` opens it from Move Selection or Details; `V` or Cancel closes it; freeze selector input and restore the same current mode, request, and focus; the user verifies this manually and no automated key-binding test is required |
| Stale request | Disable old input and replace it with current presentation data |
| Presentation failure | Apply no partial selector or Details state; cancel the gesture, close overlays, disable selector and command input, retain the last valid base display or keep the HUD hidden, and expose one typed error |
| Layout and appearance | The vertical-list structure is fixed; exact dimensions, spacing values, styling, assets, and circular-cue appearance remain user-owned |

The UI consumes presentation data. It does not calculate legality, PP,
effectiveness, targeting, or opponent selection.

## Acceptance Criteria

1. **GIVEN** the approved showcase data, **WHEN** it loads, **THEN** Charizard
   has Swift and Earthquake in slots 1 and 2, Venusaur has Vine Whip and
   Earthquake in slots 1 and 2, slots 3–4 contain no Move IDs, and every assigned
   move resolves a non-empty localized name and short description.

2. **GIVEN** each approved move is legal in a fresh Singles battle, **WHEN** Vine
   Whip is selected, **THEN** its decision contains the exact Move ID and
   sole-opponent target. **WHEN** Swift or Earthquake is selected, **THEN** its
   decision contains the exact Move ID with no selector-supplied target and
   `BattleEngine` resolves its fixed target set. In both cases, the opponent
   submits a complete legal decision, the selected move spends one PP, and the
   turn resolves.

3. **GIVEN** one opponent legal decision, **WHEN** the uniform policy runs,
   **THEN** it selects `C[0]` without an RNG draw. **GIVEN** `N = 2`, `3`, and
   `4`, **WHEN** controlled RNG returns every valid index for each `N`, **THEN**
   the operation selects the exact complete decision `C[index]` after one
   semantic traced draw, without reordering or invalid-slot retries. The test
   proves that ordinary candidates in `C` follow ascending source Move Slot
   Index and then stable active-target order rather than lexical request-array
   order, and a deterministic replay reproduces the decision and trace ordering.
   **GIVEN** built-in Struggle is the opponent's sole legal move, **WHEN** its
   candidate is built, **THEN** `C[0]` contains Struggle and the sole opponent as
   its explicit target, has no source Move Slot Index, and is selected without
   RNG; selector slot 1 is not used as equipped-slot identity.
   **GIVEN** candidate construction has zero decisions or attempts to add a
   fifth decision, **WHEN** construction validates its hard `1–4` capacity,
   **THEN** it returns `InvalidOpponentCandidateCount` before a policy input or
   RNG transaction exists, publishes nothing, performs no retry, and does not
   clamp or truncate the list.

4. **GIVEN** a move with one known typed unavailable reason, **WHEN** it is
   focused or tapped in Move Selection or Move Details, **THEN** the exact
   reason remains readable, Details stays open when already open, and no
   decision, PP, state-version, RNG, trace, or replay value changes. **WHEN**
   `BattleEngine` can determine a valid current target set independently of that
   availability restriction, **THEN** the observer-safe snapshot supplies the
   move's typed effectiveness summary. **WHEN** it cannot, **THEN** it supplies
   typed `Unknown`. The UI performs no effectiveness calculation or inference.

5. **GIVEN** a move with one PP, **WHEN** its accepted use spends that PP and
   the next request opens, **THEN** its PP is zero, it remains visible in its
   authored position, it is absent from legal decisions, and it has the
   engine-provided unavailable reason.

6. **GIVEN** assigned moves followed by blank slots, **WHEN** Move Selection
   opens, **THEN** the first legal assigned move is focused. Up and Down traverse
   assigned order, including unavailable assigned moves, and never focus a
   blank slot. Up on the top assigned move and Down on the bottom assigned move
   leave focus unchanged; no navigation wraps. Blank slots show no text, open no
   Details, and accept no confirmation.

7. **GIVEN** the user-approved positive `T_hold` and `C` as Confirm:

   - **WHEN** Confirm is released while `t < T_hold`, **THEN** exactly one tap
     result occurs for the captured current request and Move ID.
   - **WHEN** Confirm reaches `t >= T_hold` in Move Selection, **THEN** `H`
     reaches `1`, Move Details opens once, and the following release submits
     nothing.
   - **WHEN** Confirm reaches `t >= T_hold` in Move Details, **THEN** the
     circular progress completes, Details closes once, and release submits
     nothing.
   - **WHEN** Cancel is pressed in Move Details, **THEN** Details closes
     immediately without showing or waiting for circular progress.
   - **WHEN** key repeats, focus changes, input is cancelled, the application
     loses focus, Battle Info opens, or the request becomes stale, **THEN** the
     pending gesture and `H` reset without submission.
   - **GIVEN** `T_hold` is missing, non-finite, zero, or negative, **WHEN** hold
     configuration validates, **THEN** it returns `InvalidHoldThreshold` before
     selector input activates and no Confirm gesture can begin.

8. **GIVEN** Move Selection or Move Details on a current request, **WHEN** `V`
   opens Battle Info, **THEN** selector input is frozen and its mode, request,
   and focused Move ID are retained. **WHEN** `V` or Cancel closes Battle Info
   on the same request, **THEN** the exact mode and focus return without a
   decision or battle-state change. The user verifies this criterion manually;
   no automated test of the `V` binding is required.

9. **GIVEN** Move Details or Battle Info is open, **WHEN** its request becomes
   stale, **THEN** old input is disabled, the pending Confirm gesture resets,
   the overlay closes, and current presentation replaces it. The same Move ID
   retains focus when still visible; otherwise the first legal authored move is
   focused, or Struggle is focused when it is the sole legal decision. Old
   input cannot submit or change PP, RNG, trace, replay, or current battle
   state.

10. **GIVEN** all assigned moves are unusable, **WHEN** player selection opens,
    **THEN** Struggle is shown and focused in slot 1, slots 2–4 stay blank, and
    Struggle's PP is a dash.
    **WHEN** Confirm reaches `T_hold`, **THEN** full Details opens with the
    localized Struggle name and short description, Physical Category, Power
     `50`, and typed `AlwaysHits`; the tile remains typeless with a PP dash, Up
     and Down leave focus unchanged, and no Battle value changes.
     **WHEN** Struggle is tapped in Move Selection or Details, **THEN** it is
     accepted with the sole opponent as its explicit target. **GIVEN** Struggle
     is the opponent's sole legal decision after the player choice is accepted,
     **WHEN** opponent policy runs, **THEN** it selects Struggle with that same
     explicit target and without RNG.

11. **GIVEN** showcase and presentation validation, **WHEN** configuration
    differs from the exact assignments in Core Rule 1, has duplicate Move IDs
    within one Pokemon's non-empty assigned slots, a missing gameplay record, a
    missing or empty localized name or description, an unsupported effect, an
    invalid ordinary accuracy, or contradictory display sources, **THEN** it
    fails closed with the matching exact code from the Typed Error Contract:
    `ShowcaseAssignmentsMismatch`, `DuplicateAssignedMove`,
     `MissingMoveDefinition`, `MissingMovePresentation`,
     `EmptyMovePresentationText`, `UnsupportedMoveEffect`,
     `InvalidMoveAccuracy`, or `ContradictoryMoveData`. The same Move ID assigned
    once to each of two different Pokemon is valid and shares one ordinary
    presentation record.
    Fewer than four assigned moves does not fail merely because trailing slots
    are empty. A valid damaging `bAlwaysHits` record produces `AlwaysHits`, never
    numeric zero. These checks do not change the engine-wide zero-to-four move
     assignment rule.
     **GIVEN** one validation input triggers more than one listed error, **WHEN**
     it fails, **THEN** exactly one code is exposed: the first match in the
     showcase-and-presentation precedence list. In particular, a duplicate that
     also changes the exact showcase assignment returns `DuplicateAssignedMove`,
    not `ShowcaseAssignmentsMismatch` or a catch-all code.
    **GIVEN** a complete valid display was applied previously, **WHEN** a later
    selector or Details projection fails that validation, **THEN** no part of
    the candidate projection is applied, the pending gesture resets, overlays
    close, selector and command input stop, the last valid base display remains,
    and that exact presentation error code and localized text are exposed
    without any selector-to-engine call or Battle-state change. **GIVEN** no
    valid Battle display was ever
    applied, **WHEN** the same failure occurs, **THEN** the HUD and selector stay
    hidden, input stays disabled, and the typed error is exposed.

12. **GIVEN** the selector waits without input or a request update, **WHEN** a
    bounded idle test advances frames, **THEN** no selector-to-engine call,
    state-version change, RNG trace change, or replay change occurs. Refresh is
    driven only by input or an accepted request/state update.

13. **GIVEN** the opponent-only policy operation after its required ADR is
    approved:

    - **WHEN** its input is inspected, **THEN** it contains only the exact
      opponent-filtered observation, matching request identity, and complete
      legal decisions; it exposes no accepted player choice or raw core state.
    - **WHEN** a policy returns a wrong-owner, illegal-target, rejected, RNG-
      failure, invalid-candidate-count, or internal-failure result, **THEN** the
      matching `OpponentPolicyWrongOwner`, `OpponentPolicyIllegalTarget`,
      `OpponentPolicyRejected`, `OpponentPolicyRandomFailure`,
      `InvalidOpponentCandidateCount`, or `OpponentPolicyInternalFailure` code
      is returned, no opponent decision, state version, RNG progress, trace,
      immediate decision-resolution record, replay value, or queue lock changes
      from operation entry, and no retry or fallback policy runs.
    - **GIVEN** one completed opponent validation stage detects overlapping
      triggers, **WHEN** it fails, **THEN** it exposes exactly the first matching
      code in the opponent-policy precedence list; a specific code is never
      replaced by `OpponentPolicyRejected` or `OpponentPolicyInternalFailure`.
    - **WHEN** the first operation becomes stale, **THEN** all its staged work
      rolls back. At most one distinct fresh operation may run only against the
      named current opponent request while the accepted player choice remains
      valid. **WHEN** that fresh operation succeeds, **THEN** it publishes
      exactly its own decision, RNG progress, trace, immediate decision records,
      replay records, and queue lock once, while the stale operation publishes
      nothing. A second stale result halts with `OpponentPolicyStale` and no
      further retry.
    - **WHEN** the opponent operation succeeds, **THEN** its atomic boundary ends
      after opponent acceptance, action-queue locking, and publication of those
      immediate records. **WHEN** a later locked action resolves or fails,
      **THEN** ADR-0002 handles it as a separate checkpoint and it does not roll
      back the completed opponent operation, accepted decisions, or queue lock.

14. **GIVEN** the completed Singles feature in FoundationMap PIE at 1920x1080
    and 1280x720, **WHEN** Fight is selected, **THEN** the vertical four-slot
    selector replaces the command menu, assigned moves occupy their authored
    slots, trailing slots stay blank, the first legal move is focused, Up and
    Down stop at the boundaries, unavailable moves remain inspectable, and
    Cancel returns to Command Selection without a decision or Battle-state
    change.

15. **GIVEN** the user-approved `T_hold` and completed selector in FoundationMap
    PIE:

    - **WHEN** Confirm is tapped on a legal focused move in Move Selection or
      Move Details, **THEN** exactly one decision is submitted on release.
    - **WHEN** Confirm is held to `T_hold` in either mode, **THEN** progress
      reaches `H = 1`, Details opens or closes exactly once, and the following
      release submits nothing. Struggle Details show the exact system payload
      from Acceptance Criterion 10.
    - **WHEN** Battle Info opens or a request becomes stale, **THEN** input
      freezes or refreshes by Acceptance Criteria 8 and 9 without accepting old
      input.

16. **GIVEN** controlled FoundationMap PIE scenarios for Vine Whip, Swift,
    Earthquake, an unavailable ordinary move, and Struggle, **WHEN** each legal
    player decision is confirmed, **THEN** the exact required decision shape is
    accepted, the temporary opponent policy supplies its complete legal
    decision, each PP-using accepted move spends one PP, Struggle spends none,
    and the authoritative turn either presents the next current request or the
    final Battle result. **WHEN** the unavailable move is confirmed, **THEN**
    its reason is shown and no Battle value changes.

17. **GIVEN** the user has supplied final dimensions, spacing, styling,
    circular-cue appearance, move descriptions, and the `AlwaysHits` display
    treatment, **WHEN** the completed selector is viewed at actual size in
    FoundationMap PIE at 1920x1080 and 1280x720, **THEN** every required label,
    PP value, effectiveness value, unavailable reason, focus state, and hold
    state remains readable without clipping or relying only on colour, sound,
    or animation. The user alone judges layout and appearance.

## Open Questions

| Question | Owner | Resolve by | Current resolution |
|---|---|---|---|
| What positive value is `T_hold`? | User | Before selector input implementation and validation | User decision required |
| What are the final dimensions, spacing values, styling, assets, and reference art for the approved vertical list? | User | Before move-selector visual implementation | Owner-only; agents must not ask or review |
| What is the circular loading cue's appearance and motion treatment? | User | Before move-selector visual implementation | Owner-only; agents must not ask or review |
| What approved localized short descriptions are used for Swift, Vine Whip, and Earthquake, and what description is used for the separate Struggle system presentation? | User | Before ordinary move-presentation content and Struggle system-presentation authoring | Owner-only; agents must not ask or review |
| What player-facing text or symbol renders the typed `AlwaysHits` value? | User | Before move-details visual implementation | Owner-only; agents must not ask or review |
| What visual and audio treatment represents focus, Confirm, Cancel, and unavailable attempts? | User | During user-led UI work | Owner-only; agents must not ask or review |
| Will Special Effects step 6 replace Swift with Flamethrower? | User | When designing step 6 | Deferred |

The opponent-policy behavior, hard `1–4` candidate cap, `C` Confirm mapping,
`V` Battle Info mapping, input-state, and navigation rules are resolved and
closed for this design. Opponent-policy implementation remains gated by its
separate ADR. Move-presentation storage, runtime composition, and the required
observer-safe effectiveness projection extension remain gated by the separate
presentation-composition ADR that must amend or supersede the relevant ADR-0001
contract. The user-owned tuning, content, UI/UX, visual, and audio decisions
above remain required gates for their related implementation and final playable
acceptance, but they are not subjects for agent review or presentation-design
subagents.
