# Systems Index: Pokemon Solarus Fast Battle Roadmap

> **Status**: Approved
> **Created**: 2026-09-02
> **Last Updated**: 2026-09-03
> **Source Concept**: `design/gdd/game-concept.md`
> **Active Roadmap Source**: `design/quick-specs/c11a-catalog-gap-closure-addition-2026-09-02.md`
> **Active Step 1 Design Source**: `design/gdd/four-move-battle-selection.md`

---

## Overview

This index covers only the seven playable Battle steps between the completed
one-move prototype and a strategic opponent AI. It deliberately excludes the
overworld and other full-game systems. Each step must remain small and playable
while keeping generic Battle code reusable within this roadmap. Existing
BattleEngine, catalog, runtime, decision, replay, HUD, and presentation
boundaries remain the shared foundation rather than becoming new roadmap work.

This document records design order. It does not authorize implementation, Git
work, or automatic advancement from one step to the next. Every step requires
its own scope, design, and approval.

---

## Roadmap Rules

- Delivery order is strictly `1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7`.
- A later step begins only after the previous step is playable and separately
  approved.
- Generic runtime code must not branch on Charizard, Venusaur, a particular
  move, or this showcase matchup.
- BattleEngine remains the only Battle state, rule, transaction, RNG, event,
  replay, and outcome owner.
- The first step uses the approved temporary two-move loadout within a
  four-slot capacity: Charizard has Swift and Earthquake, Venusaur has Vine
  Whip and Earthquake, and slots 3–4 remain blank for both Pokemon. This proves
  the reusable selector and is not the final showcase move set. The Step 1 GDD
  supersedes the earlier prototype-roadmap shorthand that said each Pokemon
  would receive four moves in this step.
- A general learnset or TM database is deferred to step 5.
- Until strategic AI exists, the opponent selects uniformly from the current
  complete legal decisions through BattleEngine's replayable policy operation.
  One legal decision is selected without an RNG draw. Two to four legal
  decisions use exactly one semantic draw across indices `0` through
  `legal decision count - 1`. It does not retry invalid slots.
- Strategic AI later replaces the temporary choice policy, not BattleEngine's
  deterministic RNG or legal-decision contract.
- The prototype move selector uses four well-spaced rectangular move boxes in
  one vertical list. Up and Down move focus without wrapping. Exact dimensions,
  spacing values, styling, assets, and circular hold-cue appearance remain
  user-owned. The existing reusable move-tile contract remains authoritative
  for individual tiles.

---

## Systems Enumeration

| # | System Name | Category | Priority | Status | Design Doc | Hard Dependencies |
|---:|---|---|---|---|---|---|
| 1 | Four-Move Battle Selection | Gameplay | Battle Vertical Slice | Approved | `design/gdd/four-move-battle-selection.md` | Existing BattleEngine, catalog, runtime driver, decision requests, HUD contract |
| 2 | Command Child Flows | Gameplay / UI | Battle Vertical Slice | Not Started | — | Existing command and Battle decision contracts; step 1 is the delivery gate |
| 3 | First Battle Item | Gameplay | Battle Vertical Slice | Not Started | — | Step 2 Bag flow |
| 4 | Fifty-Pokemon Switching Set | Content / Gameplay | Alpha | Not Started | — | Step 2 party and switching flow; step 3 is the delivery gate |
| 5 | Move Pools for Selected Pokemon | Content / Gameplay | Alpha | Not Started | — | Step 1 move selector and step 4 Pokemon set |
| 6 | Special-Effect Expansion | Gameplay / Presentation | Alpha | Not Started | — | Step 5 move pools and existing reusable effect and presentation boundaries |
| 7 | Strategic Opponent AI | AI | Alpha | Not Started | — | Step 1 replaceable selector plus the legal actions and readable effects available after steps 5 and 6 |

The completed one-move playable battle is the implemented baseline. It is not
an eighth roadmap system.

---

## Categories

| Category | Meaning in This Roadmap |
|---|---|
| **Gameplay** | Player or opponent Battle choices and their authoritative results. |
| **UI** | Code-behind behavior and data needed for Battle interaction. Visual appearance and layout remain user-owned. |
| **Content** | Bounded Pokemon and move records needed by the current playable step. |
| **Presentation** | Readable exposure of BattleEngine results without owning gameplay state. |
| **AI** | A controller policy that selects only from BattleEngine-provided legal decisions. |

---

## Priority Tiers

| Tier | Systems | Meaning |
|---|---|---|
| **Implemented Baseline** | Existing one-move playable battle | Supplies the reusable foundation and is not new roadmap work. |
| **Battle Vertical Slice** | Steps 1-3 | Adds meaningful move choice, functional command flows, and one real item action. |
| **Alpha** | Steps 4-7 | Expands reusable content, move behavior, and opponent decision quality. |
| **Full Vision** | None | Full-game systems are deliberately outside this narrow index. |

---

## Dependency Map

### Existing Foundation

The following are reused as-is unless a later approved design proves a specific
gap:

- BattleEngine authoritative state and rule execution.
- Transactional Battle RNG, ordered events, and replay.
- Accepted catalog and runtime-scenario loading.
- Runtime driver and legal-only action-selector validation boundary. Step 1
  adds the opponent-only atomic policy operation required for engine-owned
  selection RNG and replay publication.
- Observer-safe decision requests, snapshots, and presentation data.
- Existing Battle HUD and individual move-tile contract.

### Core Layer

1. **Four-Move Battle Selection** — consumes legal Battle requests and adds the
   first multi-choice player policy plus a temporary uniform opponent policy
   through an engine-owned atomic operation.

### Feature Layer

1. **Command Child Flows** — makes Bag, Pokemon, and Run enter and leave their
   minimum functional flows safely.
2. **First Battle Item** — depends on the Bag flow.
3. **Fifty-Pokemon Switching Set** — depends on the party and switching flow.
4. **Move Pools for Selected Pokemon** — depends on the selected Pokemon set and
   the four-move selection contract.
5. **Special-Effect Expansion** — depends on supported move pools and adds
   effects in small playable groups.
6. **Strategic Opponent AI** — replaces the temporary random policy after the
   legal action space and its visible consequences exist.

### Presentation Layer

No separate presentation roadmap is created. Each step adds only the
presentation needed to make that step readable and playable. Visual layout,
styling, art, composition, and motion appearance remain user-owned.

### Polish Layer

No separate polish system enters scope before strategic AI. Polish work is
allowed only when required to make the active step readable and playable.

---

## Recommended Design Order

| Order | System | Priority | Layer | Ownership | Design Effort |
|---:|---|---|---|---|---|
| 1 | Four-Move Battle Selection | Battle Vertical Slice | Core | User decisions + Codex mechanics and code-behind | M |
| 2 | Command Child Flows | Battle Vertical Slice | Feature | User decisions + Codex mechanics and code-behind | M |
| 3 | First Battle Item | Battle Vertical Slice | Feature | User decisions + Codex mechanics and code-behind | S |
| 4 | Fifty-Pokemon Switching Set | Alpha | Feature | User content decisions + Codex data contracts and validation | M |
| 5 | Move Pools for Selected Pokemon | Alpha | Feature | User content decisions + Codex data contracts and validation | L |
| 6 | Special-Effect Expansion | Alpha | Feature | User decisions + Codex mechanics, validation, and tests | L |
| 7 | Strategic Opponent AI | Alpha | Feature | User behavior decisions + Codex AI mechanics and validation | L |

Design effort uses the map-systems convention: `S` is one focused design
session, `M` is two or three, and `L` is four or more. These are design-session
sizes, not implementation-time estimates.

---

## Circular Dependencies

None detected.

- UI consumes observer-safe Battle data and submits decisions through the
  existing control path; it does not own or feed private Battle state back into
  BattleEngine.
- The temporary selector and strategic AI consume legal decision requests; they
  do not determine Battle legality.
- Party and content data enter a Battle through an immutable setup, while Battle
  results leave through explicit result data.

---

## Bottlenecks

- **Four-Move Battle Selection** establishes the replaceable selection seam
  reused by later move pools and strategic AI.
- **Command Child Flows** provides the Bag and party paths needed by steps 3 and
  4.
- **Move Pools for Selected Pokemon** defines the action space that steps 6 and
  7 must understand.

The final strategic AI is a leaf system in this roadmap. Nothing later is
scheduled by this index.

---

## High-Risk Systems

| System | Risk | Mitigation |
|---|---|---|
| Fifty-Pokemon Switching Set | Fifty records can expand into premature full-content work. | Add only the fields needed for party setup, display, stats, and switching. |
| Move Pools for Selected Pokemon | "Move pool" could mean four equipped moves or a complete learnset. | Decide the meaning before implementation and expose only behavior the playable build supports. |
| Special-Effect Expansion | Weather, terrain, status, stat changes, Abilities, and held items can grow without a clear stop. | Add small playable groups with separate approval and readable in-battle acceptance. |
| Strategic Opponent AI | AI could bypass legal requests, inspect hidden choices, or become a broad framework. | Use the existing controller policy boundary and only permitted public or revealed information. |

---

## Progress Tracker

| Metric | Count |
|---|---:|
| Total roadmap systems | 7 |
| Design docs started | 1 |
| Design docs reviewed | 1 |
| Design docs approved | 0 |
| New MVP systems | 0 |
| Battle Vertical Slice systems designed | 1 / 3 |
| Alpha systems designed | 0 / 4 |

---

## Next Steps

- [x] Design `Four-Move Battle Selection` with
  `/design-system four-move-battle-selection`.
- [x] Complete Review 1 of
  `design/gdd/four-move-battle-selection.md` — verdict: Major Revision Needed.
- [x] Complete Review 2 of
  `design/gdd/four-move-battle-selection.md` — verdict: Needs Revision. The
  user-approved Review 2 revisions were applied on 2026-09-03.
- [x] Independent review of the revised GDD completed and was accepted by the
  project owner on 2026-09-03; it produced no separate review artifact and the
  design status is Approved.
- [ ] Scope and approve implementation separately; this index grants no code or
  asset authority.
- [ ] Advance to the next numbered system only after the current one is playable
  and separately accepted.
