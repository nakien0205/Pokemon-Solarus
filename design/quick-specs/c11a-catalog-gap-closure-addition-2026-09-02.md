# Quick Design Spec: Reusable Playable Two-Pokemon Battle

**Type:** Addition
**System:** Battle runtime orchestration and Battle HUD
**GDD Reference:** `design/gdd/game-concept.md` — Flow-State Design,
Game Pillars, and MVP Definition
**Date:** 2026-09-02
**Status:** Accepted complete — 2026-09-02

## Replacement Notice

This document completely replaces the earlier C11A catalog-gap closure proposal
that occupied this file.

The immediate goal is no longer to close the six deferred C11A catalog gaps.
Those gaps remain deferred, C11A remains incomplete, and C11B remains unchanged.
This document neither implements nor closes either package.

The existing filename is retained because the user asked to rewrite the named
document rather than delete or relocate it. The title and contents are the
current authority for this proposed prototype.

## Change Summary

Turn the existing FoundationMap battle presentation into the smallest complete
playable battle:

- one player-controlled Pokemon;
- one opponent Pokemon;
- one ordinary damage-only move for each Pokemon;
- Fight as the only functional command;
- automatic opponent action selection;
- real turn resolution and HP changes;
- fainting and a visible battle result; and
- a clean restart when PIE starts again.

The implementation must reuse the existing battle engine, runtime scenario,
Battle HUD, input, and FoundationMap. It must not create a second battle state
owner or a level-only scripted damage system.

## Motivation

Before this addition, the project could open FoundationMap, construct the Battle
HUD, display the two active Pokemon, and navigate the command menu. Confirming
Fight stopped at a UI request because no runtime owner converted that request
into a battle decision and drove the engine through a turn.

Adding more catalog content does not solve that missing playable connection.
The fastest useful milestone is therefore to finish one complete battle before
adding more Pokemon, moves, commands, items, or mechanics.

## Design Delta

The approved Game Concept already says:

> Onboarding: Begin with one move per Pokemon and one functional command.

It also says:

> Build the smallest reusable boundary needed by the current milestone,
> without implementing speculative future systems.

This spec implements those rules. It does not expand the full battle design.

Flamethrower remains Charizard's intended default move. The accepted catalog
authors it with a Burn secondary effect, while this prototype deliberately
excludes moves with special or side effects. Quick Attack is therefore a
temporary damage-only substitute. It must not become the permanent default,
and Flamethrower's authored Burn behavior must not be removed or silently
ignored to make it fit this milestone.

## Prototype Content

| Side | Pokemon | Move | Current move behavior |
|---|---|---|---|
| Player | Charizard | `Move.QuickAttack` (Quick Attack), temporary | Selected-opponent physical damage, normal accuracy, contact, priority +1, and no secondary effect |
| Opponent | Venusaur | `Move.VineWhip` (Vine Whip) | Selected-opponent physical damage, normal accuracy, contact, priority 0, and no secondary effect |

Both move definitions already exist in the accepted catalog. Each contains one
Damage descriptor and no status, weather, terrain, stat-stage, volatile, field,
side-condition, item, or other secondary descriptor.

The existing reusable stat, damage, type, accuracy, PP, priority, targeting,
fainting, and outcome rules remain authoritative. This prototype must not
hardcode damage values, HP values, move outcomes, or a winner in the runtime
orchestration.

The current scenario may still naturally favor Charizard. The reusable loop
must nevertheless handle either side winning if later data changes.

## Player Flow

1. The user opens FoundationMap and starts PIE.
2. The existing Battle HUD shows Charizard, Venusaur, and their authoritative
   current and maximum HP.
3. Fight is available. Bag, Pokemon, and Run remain visible but unavailable.
4. The user confirms Fight.
5. Command input is disabled while the turn resolves, preventing a duplicate
   submission.
6. The player selection policy converts the current observer-safe request into
   a Fight decision using its one legal move and one legal target.
7. The temporary opponent policy answers the opponent request using its one
   legal move and one legal target. This is automatic selection, not strategic
   AI.
8. The runtime driver advances the existing BattleEngine through the locked
   actions, move commitment, target resolution, move effects, and end-turn
   boundary.
9. The HUD immediately presents the authoritative HP after the completed turn.
10. If the battle is still active, the next valid command request is presented
    and Fight becomes available again.
11. If one side has won, the final HP remains visible, command input stays
    disabled, and the existing battle text area shows a short result:
    `You won.` or `You lost.`
12. Stopping and restarting PIE creates a fresh battle from the runtime
    scenario.

There is no separate move-selection screen in this prototype because each
Pokemon has exactly one legal move.

## Reusable Runtime Boundary

The implementation is reusable only if all of the following remain true:

- The runtime driver receives battle decisions and advances BattleEngine
  phases; it does not know Charizard, Venusaur, Quick Attack, or Vine Whip.
- Pokemon, stats, moves, PP, and starting active slots continue to come from the
  runtime scenario and accepted catalog.
- The one-move player and opponent policies read legal options from the current
  `FBattleDecisionRequest`. They do not manufacture move or target IDs.
- The one-move policies are kept separate from the generic runtime driver.
  Later move-selection UI, random opponent selection, and strategic AI can
  replace those policies without replacing the driver.
- If a request exposes zero or more than one legal move or target, the current
  one-move policy stops with a visible error instead of silently choosing an
  arbitrary option. Four-move selection belongs to the next roadmap step.
- `FBattleEngine` remains the only battle-state, transaction, RNG, event, and
  outcome owner.
- The HUD receives observer-safe snapshots and display-ready values. It never
  reads or mutates private battle state.
- No species-specific, move-specific, or match-specific branch is added to the
  generic driver.

This boundary lets future single battles reuse the same loop by changing
scenario data and selection policies.

## Failure Behavior

If a decision cannot be created, a submission is rejected, a required engine
phase fails, or the runtime cannot reach the next request or terminal outcome:

- stop advancing immediately;
- keep command input disabled;
- keep the last valid Battle HUD state visible;
- show `Battle error.` in the existing battle text area; and
- log the exact rejected step and reason.

The driver must use a small phase-advance guard so an unexpected state cannot
create an infinite loop in PIE.

## Affected Systems

| Area | Impact | Required result |
|---|---|---|
| Runtime scenario | Replace Charizard's effect-bearing move with an existing damage-only move | The playable scenario has exactly one damage-only move per Pokemon |
| Battle GameMode | Bind the HUD request, create the one-move decisions, drive the turn, and refresh presentation | Fight completes a real reusable BattleEngine turn |
| Battle HUD code-behind | Present terminal or error text while retaining final HP and disabling input | Victory, defeat, and failure are readable without a new layout |
| Existing BattleEngine | Used as-is | No new state owner, damage shortcut, or duplicate rules |
| FoundationMap and Blueprint visuals | Reused as-is | No visual or asset-layout change |

## Proposed Implementation File Map

This is the expected implementation write set. It is not authorization to edit
these files yet.

### Source scenario

- `Game/SourceData/Battle/Initial/runtime_scenario.json`
  - Temporarily uses `Move.QuickAttack` until Flamethrower's complete authored
    behavior is allowed in the playable milestone.
- `Game/Content/Data/Battle/Initial/DT_BattleRuntimeScenario.uasset`
  - Mirrors the approved source scenario for PIE.

### Runtime orchestration

- `Game/Source/PokemonSolarus/Public/UI/BattleGameMode.h`
  - Declares command binding, one-move selection, guarded runtime advancement,
    and terminal presentation ownership.
- `Game/Source/PokemonSolarus/Private/UI/BattleGameMode.cpp`
  - Implements the reusable decision-to-turn loop without identity-specific
    branches.

### Existing HUD facade

- `Game/Source/PokemonSolarus/Public/UI/BattleHUDWidget.h`
  - Exposes one code-behind method for terminal or failure text.
- `Game/Source/PokemonSolarus/Private/UI/BattleHUDWidget.cpp`
  - Reuses the existing Battle text signal and disables command input without
    changing layout or styling.

No BattleEngine, executor, controller, input asset, Widget Blueprint, map,
sprite, material, texture, animation, or audio file is expected to change.
If implementation proves another path is required, stop and return with the
exact reason before editing it.

## Validation

Only validation needed to prove this prototype playable is included:

1. Build `PokemonSolarusEditor Win64 Development` once.
2. Open FoundationMap and run one real PIE battle.
3. Confirm the starting names and HP are visible.
4. Confirm only Fight is functional.
5. Confirm each Fight press resolves exactly one complete turn.
6. Confirm both Pokemon use their one authored move and HP changes match the
   authoritative runtime state.
7. Confirm input cannot submit twice during resolution.
8. Continue until one Pokemon faints.
9. Confirm final HP, fainted state, and the correct result text remain visible,
   with command input disabled.
10. Stop and restart PIE; confirm the initial battle starts cleanly again.
11. Check the PIE log for Battle errors, rejected transitions, binding errors,
    or crashes.

No full Battle Automation suite, forced-Unity build, evidence root, hash
manifest, independent review, or publication gate is required for this
prototype. A successful build without the manual PIE flow is not sufficient to
call it playable.

## Acceptance Criteria

- [x] FoundationMap starts a visible Charizard-versus-Venusaur battle.
- [x] Each Pokemon has exactly one legal move with Damage as its only effect
      descriptor.
- [x] Fight confirmation submits a real player decision.
- [x] The opponent automatically submits its sole legal move.
- [x] The existing BattleEngine resolves ordering, PP, targeting, accuracy,
      damage, fainting, and outcome.
- [x] Both HP panels show authoritative post-turn values.
- [x] The next Fight request appears after every non-terminal turn.
- [x] Victory or defeat leaves final HP visible, disables input, and shows the
      correct short result text.
- [x] Restarting PIE starts a clean battle.
- [x] The runtime driver contains no Pokemon, move, damage, HP, or winner
      special case.
- [x] Future selection policies can replace the one-move policies without
      replacing the runtime driver.
- [x] A normal Editor build and one complete manual PIE battle pass.
- [x] No unrelated file changes.

## Acceptance Record

- **Owner decision:** Accepted complete by the project owner on 2026-09-02.
- **Normal Editor build:** `PokemonSolarusEditor Win64 Development` succeeded
  on 2026-09-02; UnrealBuildTool reported the target was already up to date.
- **Manual playable proof:** The project owner reported one complete
  FoundationMap PIE battle on 2026-09-02.
- **Scope:** This acceptance closes only this one-Pokemon playable prototype.
  It does not close any C11A catalog gap or change C11B.

## Explicitly Excluded From This Prototype

- Closing any C11A catalog gap or changing C11A/C11B completion status.
- Cryogonal, Rampardos, Dragapult, Waterfall, Breaking Swipe, Infiltrator, or
  Icy Rock.
- A four-move player selector.
- Functional Bag, Pokemon, or Run commands.
- Bag items.
- Party switching or reserve Pokemon.
- Additional Pokemon or move pools.
- Weather, terrain, major status, volatile status, stat-stage, Ability, held
  item, or other special-effect presentation work.
- Strategic opponent AI.
- New UI layout, styling, Blueprint visual logic, sprites, textures, animation,
  audio, camera work, or map edits.
- Broad engine cleanup, refactoring, new public BattleEngine APIs, or replay
  changes.
- Full production validation, release evidence, roadmap-status changes, or Git
  operations.

## GDD Reconciliation

Completed on 2026-09-02. The GDD and its settled-requirements handoff now state
that Flamethrower remains Charizard's intended default, Quick Attack is a
temporary damage-only substitute, and BattleEngine is the sole authority for
damage, HP, fainting, and outcome.

## Approval Boundary

The original design-only authorization was later followed by the scoped
implementation and this explicit owner acceptance. It did not authorize Git
operations.

## Roadmap After This Prototype

Each step begins only after the previous step is playable. Each step needs its
own exact scope and approval; this roadmap does not authorize automatic
implementation.

1. **Give both Pokemon four moves.**
   - Add a player move-selection flow.
   - Give Charizard and Venusaur four usable move slots each.
   - Every selected move must perform all behavior it claims.
   - Start with damage-only moves so unsupported special effects are not
     silently ignored.
   - Until strategic AI exists, the opponent uniformly selects one legal move
     slot from indices `0` through `3`.

2. **Make Bag, Pokemon, and Run functional.**
   - Bag opens and can return safely even before it contains usable items.
   - Pokemon opens the party/switching flow.
   - Run follows the encounter's legal or blocked result.

3. **Add the first simple Bag item.**
   - Start with one ordinary item, such as a basic HP-restoring item.
   - Prove inventory count, target selection, application, and consumption
     before adding more item families.

4. **Add more Pokemon for switching.**
   - Select 50 Pokemon.
   - Add them first as reusable template records sufficient for party setup,
     display, stats, and switching.
   - Do not pretend their complete move pools or special behaviors exist yet.

5. **Add move pools for the 50 selected Pokemon.**
   - Decide explicitly whether “move pool” means four equipped battle moves or
     a larger learnset before implementation.
   - Only expose moves whose currently claimed behavior works.
   - Keep unsupported special-effect moves unavailable until the next step.

6. **Add special effects.**
   - Expand in small playable groups: weather, terrain, major status, volatile
     status, stat changes, Abilities, held items, and other move effects.
   - Each effect must be readable in the playable battle, not merely present in
     backend tests.

7. **Add a strategic AI opponent.**
   - Replace the temporary random legal move-slot selection.
   - AI must use the same legal requests and battle information available under
     its controller policy; it must not bypass BattleEngine rules.

8. **Choose what comes next from the playable game.**
   - Reassess the actual prototype after the AI battle works.
   - Select the next milestone from observed player needs rather than guessing
     it now.
