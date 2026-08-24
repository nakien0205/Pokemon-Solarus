# Game Concept: Pokemon Solarus

*Created: 2026-08-19*  
*Status: Approved*

## 1. Overview

### Elevator Pitch

Pokemon Solarus is a free, single-player Windows PC Pokemon fangame about
building a team, progressing through a still-undefined region, and eventually
challenging the Pokemon League. Development begins by proving one polished,
readable Charizard-versus-Venusaur battle before expanding into the wider game.

### Core Identity

| Aspect | Decision |
| --- | --- |
| Genre | Single-player, turn-based monster-collecting RPG |
| Platform | Windows PC |
| Target audience | Players who want a familiar Pokemon journey with readable, satisfying battles; exact age and experience range are not yet defined |
| Player count | One |
| Session length | Not decided |
| Monetization | None; free and public |
| Estimated scope | Large, solo project; full timeline intentionally not estimated |
| Current milestone | One good battle |
| Engine | Unreal Engine 5.8.1 |
| Comparable references | Pokemon Scarlet/Violet, Pokemon Red, and Pokemon Gamma Emerald |

### Unique Hook

The full-game hook has not been decided.

The current presentation signature is animated 2D Pokemon and trainers staged
inside a 3D world, with readable modern battles and camera work designed around
the available sprite angles. This is an approved direction, but it is not being
claimed as the final unique hook for the entire game.

### Inspiration and References

| Reference | What Solarus Uses | Boundary |
| --- | --- | --- |
| Pokemon Gamma Emerald | Animated 2D characters in 3D battle environments | A presentation principle, not a specification to copy |
| Pokemon Scarlet/Violet | Baseline for modern battle behavior | Explicit Solarus decisions override it |
| Generation III | Fallback behavior when the modern version does not fit Solarus | It does not replace the modern ruleset |
| Pokemon Red | Rough initial reference for eventual world size | The region and content count remain unknown |

Non-game visual and narrative references have not been selected.

## 2. Player Fantasy

### Core Fantasy

The player becomes a Pokemon Trainer, builds and develops a team, overcomes
increasingly meaningful battles, and progresses toward the Pokemon League.

The exact region, story, characters, side objective, evil organization, and
final roster are intentionally unknown.

### Player Experience Analysis

#### Target Aesthetics

The current milestone supports this provisional order:

| Aesthetic | Priority | Delivery |
| --- | --- | --- |
| Challenge | 1 | Clear turn-based choices and understandable battle consequences |
| Sensation | 2 | Animated sprites, impact timing, audio, HP movement, and controlled camera presentation |
| Fantasy | 3 | The experience of commanding Pokemon in battle |
| Discovery | Not ranked | Depends on the future region and exploration design |
| Narrative | Not ranked | Story has not been designed |
| Expression | Not ranked | Future team-building choices are expected, but their scope is unknown |
| Submission | Not ranked | Relaxation and comfort goals have not been decided |
| Fellowship | N/A for multiplayer | Solarus is single-player; character relationships remain undecided |

Approval of this document does not permanently rank the undecided full-game
aesthetics.

#### Key Dynamics

Players should naturally:

- Read the battlefield before choosing an action.
- Learn how moves, types, status effects, switching, and targeting interact.
- Recognize important results through synchronized visual, audio, text, and HP
  feedback.
- Develop stronger teams and tactical knowledge as the future game progresses.
- Trust that enemies follow the same visible battle rules unless a scripted
  exception is clearly communicated.

#### Core Mechanics

1. Turn-based Pokemon battles.
2. Pokemon party building and progression.
3. Capturing and developing Pokemon.
4. Future region exploration and quests.
5. Progress toward a Pokemon League.

Only the first mechanic is currently defined in enough detail for the first
prototype.

### Player Motivation Profile

| Need | How Solarus Supports It | Strength |
| --- | --- | --- |
| Autonomy | Move, target, item, switching, party, and later team-building choices | Core in the full battle system; deliberately minimal in the first placeholder |
| Competence | Clear rules, readable feedback, tactical learning, and progression | Core |
| Relatedness | Future connections to Pokemon, partners, and characters | Supporting, but not yet designed |

#### Player-Type Appeal

- **Primary: Achievers** — team growth, progression, battle victories, quests,
  and reaching the League.
- **Secondary: Explorers** — learning battle systems and eventually discovering
  the region.
- **Socializers:** Character-based appeal may exist later, but multiplayer is
  not planned.
- **Competitors:** PvP, rankings, and leaderboards are not part of the concept.

Market and audience validation have not been performed. Commercial success is
not the project's stated goal.

### Flow-State Design

- **Onboarding:** Begin with one move per Pokemon and one functional command.
- **Growth:** Expand to the four-move showcase only after the small battle
  works.
- **Feedback clarity:** Every meaningful battle result must have readable state
  and synchronized feedback.
- **Difficulty scaling:** Future full-game scaling remains undecided.
- **Failure recovery:** The reusable system supports defeat, but the initial
  placeholder intentionally has no reachable loss path.

### Target Player Profile

| Attribute | Current Decision |
| --- | --- |
| Age range | Not decided |
| Gaming experience | Not decided; battle information should remain readable without assuming expert knowledge |
| Time availability | Not decided |
| Platform preference | Windows PC, using keyboard or a supported controller |
| Current games | Not defined as an audience requirement |
| Looking for | A complete single-player Pokemon journey with satisfying, understandable battles |
| Likely turnoffs | Unclear results, mandatory online play, or spectacle that makes battle state difficult to read |

### Visual Identity Anchor

**Direction:** Animated 2D Pokemon in a 3D Battle Stage

**Visual rule:** Battles must make animated sprites feel alive inside a grounded
3D arena without sacrificing readability or exposing unsupported sprite angles.

Supporting principles:

- **Sprite readability first:** Camera placement must support available front
  and back animation sets.
- **Clear stage composition:** The player's large back sprite sits near the
  left foreground, the opponent occupies the midground, and the interface
  remains easy to scan.
- **Motion with restraint:** Animation, impact feedback, and later camera cuts
  add energy without hiding battle information.

**Color philosophy:** The overall palette is not decided. Approved type and
status colors must remain readable at their real display size and must not
communicate essential information through color alone.

## 3. Detailed Rules

### Core Loop

#### Moment to Moment

Choose a legal command, move, and target; watch actions resolve; read the
updated battle state; then make the next decision.

#### Short Term

Complete an encounter while managing HP, status, move availability, targeting,
switching, and items when those systems are enabled.

The initial placeholder intentionally reduces this to selecting `Attack` and
observing a complete battle result.

#### Session Level

The eventual session loop will include exploration, quests, battles, team
management, and progression. Its exact structure and natural stopping points
are unknown because the region and overworld have not been designed.

#### Long-Term Progression

Develop a Pokemon team, advance through the future story and region, and
eventually challenge the Pokemon League. The exact progression structure is
deferred.

#### Retention Hooks

- **Curiosity:** Future region, Pokemon, quests, and story discoveries.
- **Investment:** Pokemon growth and team development.
- **Mastery:** Better understanding of battle rules and team strategy.
- **Social:** No multiplayer retention system; future character relationships
  remain unknown.

### Game Pillars

#### Pillar 1: One Good Battle First

Solarus must prove that one battle feels complete before development expands
into the wider game.

*Design test:* When choosing between beginning overworld content and improving
an incomplete battle, finish the battle first.

#### Pillar 2: Familiar Strategy, Explicit Solarus Rules

Battles should feel recognizably like modern Pokemon while documenting every
intentional Solarus exception.

*Design test:* When no Solarus rule exists, use verified Scarlet/Violet
behavior, then verified Generation III behavior only when the modern version
does not fit, and otherwise ask.

#### Pillar 3: Readability Before Spectacle

Players must always understand what happened, why it happened, and what
decision is required next.

*Design test:* If an effect looks impressive but obscures targeting, timing,
HP, or results, simplify the effect.

#### Pillar 4: Small Now, Reusable Later

The current build remains tiny while using clean seams that can support the
eventual full battle system.

*Design test:* Build the smallest reusable boundary needed by the current
milestone, without implementing speculative future systems.

#### Pillar 5: A Complete Single-Player Journey

The long-term destination remains a full region, progression path, quests,
story, and Pokemon League even though those elements are not designed yet.

*Design test:* Future features must support the single-player journey rather
than online or competitive-service goals.

### Anti-Pillars

- **Not multiplayer:** Online play would compromise the approved single-player
  scope.
- **Not a full world before the battle works:** Premature overworld development
  would compromise One Good Battle First.
- **Not spectacle over information:** Presentation may not hide battle state or
  input focus.
- **Not premature framework building:** Unneeded GAS, replication, CommonUI
  architecture, and advanced rendering would compromise the small milestone.
- **Not invented lore:** Region, story, organization, side objective, and
  roster decisions remain unknown until deliberately designed.
- **Not transformation-gimmick driven:** Mega Evolution, Dynamax, and
  Terastallization are not currently included.

### MVP Definition

**Core hypothesis:** A clear, responsive turn-based battle using animated 2D
Pokemon in a 3D arena can feel good enough to justify developing the rest of
Pokemon Solarus.

#### Required for the Initial Placeholder

1. One simple 3D arena and one fixed camera.
2. Charizard and Venusaur with 200 HP each.
3. `Attack` as the only functional command.
4. Disabled visible placeholders for Bag, Pokemon, and Run.
5. Charizard using Flamethrower for 80 fixed damage.
6. Venusaur using Vine Whip for 50 pure damage.
7. Both moves always hitting.
8. HP bars, action resolution, fainting, victory, and a battle result.
9. Charizard always winning.
10. Reusable battle seams rather than one level-only script.

#### Explicitly Outside the Initial Placeholder

- Full official damage calculations.
- A reachable player-loss path.
- The other six showcase moves.
- Strategic opponent AI.
- Dynamic cameras.
- The overworld, region, quests, story, saving, and broader progression.
- Final art, audio, and content pipelines.

### Scope Tiers

| Tier | Content | Features | Timeline |
| --- | --- | --- | --- |
| Initial MVP | One arena, Charizard, Venusaur, and two move presentations | One functional command and complete victory flow | One-week target recorded in the handoff |
| Battle Vertical Slice | The four-move Charizard-versus-Venusaur showcase | Full command menu, one Hyper Potion, party view, blocked Run, richer rules and presentation | Not estimated |
| Full-Game Alpha | Entire planned journey in unfinished form | Not sufficiently defined | Not estimated |
| Full Vision | Region, story, quests, progression, League, and final content | Complete polished single-player fangame | Not estimated |

## 4. Formulas

The initial placeholder formula is retained as milestone history. The current
global base-damage rule follows it.

For the initial placeholder:

```text
HP After Damage = max(0, HP Before Damage - Fixed Damage)
```

Approved values:

- Charizard maximum HP: `200`
- Venusaur maximum HP: `200`
- Flamethrower fixed damage: `80`
- Vine Whip pure damage: `50`
- Placeholder accuracy: always hits

### Global Base Damage Rule

All damaging moves use the same base-damage calculation. The calculation
accepts the attacker's level, the move's power and damage category, and both
Pokemon's calculated battle stats. It must not depend on a specific Pokemon or
move identity.

- Physical moves use the attacker's Attack and the defender's Defense.
- Special moves use the attacker's Special Attack and the defender's Special
  Defense.
- Status moves do not use the damage calculation.

```text
Level Factor = floor((2 * Attacker Level) / 5) + 2
Scaled Damage = floor(Level Factor * Move Power * Offensive Stat / Defensive Stat)
Base Damage = floor(Scaled Damage / 50) + 2
```

Each shown division rounds down before the next step.

This is the global base-damage stage, not the complete final-damage process.
STAB, type effectiveness, random rolls, critical hits, burn, and other damage
modifiers are not part of this rule yet. They will be added later as separately
approved global rules.

The authoritative battle handoff governs all other settled full-system
formulas and rules. This document does not replace or reinterpret them.

The exact Solarus Run formula is settled in the battle rules: use permanent,
unmodified Speed, reject either Speed below `4`, then calculate
`F = floor((PlayerSpeed * 32) / floor(WildSpeed / 4)) + 30 * C`. `C` starts at
`1`, increments only after a legal failed Run, and never resets. `F > 255`
succeeds without RNG; otherwise draw `U[0,255]` and succeed only when `R < F`.
Cry for Help and wild reinforcement are **Freeze until call by user**; existing
related code remains unchanged.

## 5. Edge Cases

### Known Boundaries

- The one-move placeholder comes before the four-move showcase.
- The full game exists as a vision, but its world and story are not yet
  designed.
- The initial placeholder cannot produce player defeat.
- The reusable battle system must eventually support defeat even though the
  placeholder cannot reach it.
- The exact official effectiveness-label unlock trigger is unproven; Solarus
  uses its documented project-specific rule.
- Battle Info layout and save timing remain deferred. Cry for Help and wild
  reinforcement are **Freeze until call by user**.

### Design Risks

- The battle may function correctly without yet feeling satisfying.
- A battle-focused prototype cannot validate the future exploration or story
  loops.
- The full game lacks a final unique hook beyond its current battle
  presentation direction.

### Technical Risks

- Animated 2D sprites must remain visually convincing inside a 3D arena.
- Camera work must avoid unsupported sprite angles.
- Reusable battle behavior could become overly complex before the milestone
  needs it.
- The full modern battle rules represent much more work than the initial
  placeholder.

### Market Risks

- Audience positioning has not been validated.
- The project is free and public; commercial market performance is not a stated
  objective.
- Release-related questions remain outside this document.

### Scope Risks

- A complete Pokemon-sized game is a very large solo project.
- Region, story, quests, progression, roster, art, animation, audio, and battle
  content can multiply production time.
- Treating the completed battle interview as permission to implement every
  documented rule immediately would overwhelm the current milestone.

### Open Questions

- What is the region?
- What is the story?
- What is the side objective?
- Is there an evil organization?
- What is the final Pokemon and Fakemon roster?
- What is the final full-game hook?
- What is the visual identity outside battles?
- What are the intended session length and target-player details?
- What is the full-game timeline?
- What is the Battle Info layout?
- When does the future game save?
- Cry for Help and wild reinforcement remain **Freeze until call by user**.

These questions are intentionally deferred rather than missing through
oversight.

## 6. Dependencies

### Technical Considerations

| Consideration | Assessment |
| --- | --- |
| Engine | Unreal Engine 5.8.1, already installed and pinned |
| Language | C++ primary; Blueprints permitted for focused gameplay prototyping |
| Key challenge | A reusable, readable battle loop without premature full-game architecture |
| Art style | Animated 2D Pokemon and trainers in a 3D environment |
| Art complexity | Moderate for the prototype; full-game asset burden is not estimated |
| Audio | Placeholder audio initially; complete battle presentation needs clear UI, impact, cry, and move audio |
| Networking | None |
| Prototype volume | One arena, two Pokemon, two initial move presentations, later eight showcase moves |
| Full content volume | Unknown |
| Procedural generation | None approved |

### Authoritative Documents

- `docs/battle-system-interview-handoff.md` owns settled battle requirements.
- `UE.md` owns the approved first-battle engine boundary.
- `.codex/docs/technical-preferences.md` owns current technical constraints.
- Future system GDDs must not silently override these sources.

`design/gdd/systems-index.md` remains absent and will not be created in this
session.

## 7. Tuning Knobs

The following approved settings can later be configured without changing the
concept:

- Text Speed: Slow, Normal, Fast.
- Battle Animation Speed: Normal, Fast.
- Battle Animations: On, Off.
- Hit Flash: Normal, Reduced, Off.
- Camera Shake: Normal, Reduced, Off.
- Controller vibration: On, Off.
- Battle style: Shift or Set where eligible.
- EXP distribution: Party-Wide or Participant-Only.
- Partner control: AI, player-controlled, or ask before the battle.

The placeholder's 200 HP, 80 damage, 50 damage, always-hit behavior, and
command availability remain recorded as fixed historical milestone decisions,
not current global battle values or tuning knobs.

Full-game content quantity, difficulty curve, and progression pacing are not
yet defined.

## 8. Acceptance Criteria

This concept is approved when:

- Pokemon Solarus is presented as a future full single-player game.
- One good battle is clearly the current milestone.
- The one-move placeholder clearly precedes the four-move showcase.
- No settled battle decision is contradicted.
- Unknown world, story, roster, and scope decisions remain unknown.
- The hybrid 2D-in-3D battle direction is preserved.
- Windows PC and Unreal Engine 5.8.1 remain the approved platform and engine.
- The document uses all eight required GDD sections.
- No systems index, architecture, art bible, Unreal project, implementation, or
  Git repository is created as part of this approval.

### Deferred Next Steps

These are not authorized or started by this document:

1. Finish approval and write this concept document.
2. In a separate session, obtain explicit approval before creating the real
   `.uproject`.
3. Build the initial one-move battle placeholder.
4. Validate the placeholder before expanding to the four-move showcase.
5. Leave art-bible, map-systems, design-system, architecture, and later
   production workflows for separately approved work.

Engine setup is already complete and must not be repeated.
