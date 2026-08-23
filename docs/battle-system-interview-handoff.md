# Pokemon Solarus Battle-System Interview Handoff

**Status:** Pre-GDD battle requirements interview complete  
**Last updated:** 2026-08-19  
**Last completed answer:** 394  
**Exact continuation point:** The user explicitly closed the interview at
question 394. There is no pending question 395.  

## Purpose

This file preserves the user's settled battle-system decisions so a fresh
session can continue without repeating hundreds of questions.

This is a handoff, not an approved GDD, architecture, implementation plan, or
authorization to create the Unreal project. When this file conflicts with an
older generic reference, the latest user decision recorded here governs the
battle-design interview. Any conflict with a current approved project document
must be shown to the user before either document is changed.

## Required Read Order for Battle Design Work

1. `docs/battle-system-interview-handoff.md` -- this file, completely.
2. `CLAUDE.md` -- collaboration and write-approval rules.
3. `UE.md` -- first-battle engine boundary.
4. `.codex/docs/technical-preferences.md` -- current technical constraints.

Do not reopen settled questions or resume the questionnaire unless the user
explicitly reopens the interview.

## Workflow and Hard Boundaries

- Work in `D:\Python\Projects\Pokemon Solarus`.
- Use plain, simple language and number only real questions. Use bullets or
  letters for examples and subpoints so question numbering remains stable.
- The interview is complete. Do not invent missing design decisions or turn
  known deferrals into assumed answers.
- Assess whether a user proposal is suitable and explain a better option when
  it is not.
- For an unsettled battle mechanic, use this decision order: an explicit
  Solarus rule first; otherwise verified Scarlet/Violet behavior; otherwise
  verified Generation III behavior if the modern version does not fit
  Solarus; ask the user if neither reference resolves it. Do not replace the
  modern ruleset wholesale with Generation III mechanics.
- Do not edit a GDD, game concept, architecture, or implementation until the
  user has reviewed a draft and explicitly approved the named write.
- Do not create a `.uproject` without separate approval.
- Do not initialize Git or commit anything. The workspace is not a Git
  repository.
- Do not launch Unreal merely to repeat setup checks.
- Do not repeat the disposable Unreal capability tests; they already passed.
- Use `.codex/` paths. Some inherited documents still mention `.claude/`, but
  `.codex/` is the real repository path.
- Keep GAS, replication, full CommonUI architecture, advanced rendering, and
  unrelated Unreal systems outside the first-battle scope.
- Do not restart release or legal discussion unless the user explicitly asks.
- `art-bible`, `map-systems`, and `estimate` remain deferred workflows. Do not
  force them into the battle interview.

## Engine Setup Status

- Unreal Engine is pinned to **5.8.1**, changelist **56057345**.
- Installed compatible changelist: **55116800**.
- Installed branch: `++UE5+Release-5.8`.
- The approved engine-setup documentation changes were completed and
  validated.
- No `.uproject` exists.
- Git is not initialized.

## Project Direction

Pokemon Solarus is ultimately intended to be a complete, free, public,
single-player Pokemon fan game with a region, story, quests, progression, and a
Pokemon League. The initial world-size reference is roughly Pokemon Red, but
the region, side objective, evil organization, story, and complete species
roster have not been decided.

The roster may contain official Pokemon and original Pokemon/Fakemon chosen
from across generations. Do not invent the region or story yet. The current
priority is a reusable battle system that can support the eventual full game.

The planned game modes are Casual, Nuzlocke, and Champion. Only **Casual** is in
scope now. A player chooses one mode for the entire game. Nuzlocke and Champion
must not be designed or implemented until the larger project is complete.

## Reference Principles

- Gamma Emerald is the main battle-presentation reference: animated 2D Pokemon
  and trainers inside a 3D environment.
- Use reference games as principles, not specifications to copy blindly.
- The desired command view places the player's large back sprite in the
  near-left foreground, the opponent's front sprite in the center/midground,
  HUD information above, and commands near the lower right.
- Modern Pokemon-style idle camera cuts should show varied battlefield,
  Pokemon, and trainer angles instead of holding one angle forever.
- The battle rules should feel like a normal, readable Pokemon battle: choose a
  command, choose a move or target where needed, resolve turns, and continue.

## Delivery Ladder

### Documentation Drift to Preserve, Not Silently Fix

`UE.md` and `.codex/docs/technical-preferences.md` currently describe a
four-move two-Pokemon battle as the first-battle boundary. The later interview
clarified a smaller implementation step: one move per Pokemon first, followed
by the four-move showcase. Treat the one-move version as the initial placeholder
and the four-move version as the showcase target. Do not silently rewrite the
approved engine documents; present a proposed reconciliation and obtain the
user's approval before changing them.

### Initial One-Week Placeholder

This is the first implementation target, not the finished battle sequence.

- One simple 3D arena and one fixed camera.
- Two placeholder Pokemon: Charizard and Venusaur.
- Both have hard-coded **200 HP**.
- Only HP exists. Attack, Special Attack, Defense, Special Defense, and Speed
  are not part of this placeholder.
- `Attack` is the only functional command.
- Bag, Pokemon, and Run are visible disabled placeholders.
- Charizard has only Flamethrower, dealing a fixed **80 damage**.
- Venusaur has only Vine Whip, dealing a fixed **50 pure damage**.
- Both moves always hit in this placeholder.
- Include HP bars, fainting, victory, and a battle result.
- Charizard must always win. A reachable player-loss path is intentionally
  deferred until fuller move pools exist.
- Use only the current Flamethrower and Vine Whip presentations. Do not pretend
  the other showcase moves exist yet.
- Although tiny, build the battle loop through reusable seams rather than a
  throwaway level-only script.

### Final Two-Pokemon Showcase

This is the intended finished battle sequence for the two-Pokemon showcase,
not the entire eventual game.

- Charizard moves: Flamethrower, Fly, Dragon Pulse, Slash.
- Venusaur moves: Leaf Storm, Protect, Toxic, Earthquake.
- Use canonical move behavior once the full rules are enabled.
- Commands: Fight, Bag, Pokemon, Run.
- Fight opens the four-move selection UI.
- Bag contains one Hyper Potion that heals **120 HP**.
- Pokemon opens a party UI showing only the current Pokemon in slot one.
- Run from this trainer battle returns a clear `Cannot run from a Trainer
  battle` message.
- Show HP, major status, fainting, victory, and defeat support in the reusable
  system even though the first placeholder cannot lose.
- The Charizard-favored matchup is intentional.
- Add the polished 2D animation, move presentation, dynamic camera, trainer
  presentation, and 3D arena incrementally after the functional placeholder.

## Full Reusable Battle Rules

### Baseline and Scope

- Use Pokemon Scarlet/Violet-style modern core rules as the baseline, with
  every Solarus exception documented explicitly.
- Do not add Mega Evolution, Dynamax, or Terastallization now.
- Support wild, ordinary Trainer, rival, boss/Gym, tutorial/scripted, and
  partner battles.
- Support Single Battles, Double Battles, and partner Double Battles.
- The current maximum is two active Pokemon per side. Triples and quadruples
  are future design work, not a present promise.
- Party size is six.
- Normal Pokemon stats, formulas, IVs, EVs, natures, base stats, move power,
  accuracy, critical hits, STAB, and type matchups follow modern official rules
  when the full reusable layer is enabled.
- Level cap is 100.
- Use the existing 18 official types. Do not add a new type now.
- Move categories are Physical, Special, and Status.
- Build moves from reusable effects and data, not one bespoke code path per
  move.
- Major status conditions are mutually exclusive. Volatile effects may
  coexist as official rules allow.
- Use Freeze, not Frostbite.
- Stat stages use the official `-6` to `+6` range and canonical multipliers.
  The earlier `-5` to `+5` and flat 20% idea was rejected because it would not
  follow the chosen official baseline.
- Accuracy and evasion use official stages and calculations.
- Weather, terrain, entry hazards, screens, rooms, Abilities, and held items
  should be supported by reusable architecture eventually, but content is
  implemented only when the current milestone needs it.
- If Fight is selected and no move has PP, use Struggle. Bag, Pokemon, and Run
  remain available when legal.
- Unusable moves remain visible and greyed out with a clear reason.
- There is no overworld poison or other status damage. Persistent status damage
  happens only during battle.

### Outcomes

Supported outcome categories are:

- Victory.
- Defeat.
- Escape.
- Capture-based victory.
- Explicit scripted interruption or ending.

There is no normal draw outcome. If the final usable Pokemon on both sides
faint from the same resolution, the player receives **Defeat**. Eligible EXP
from the final opponent can still be awarded before defeat cleanup.

On ordinary player defeat:

- Apply the configured money loss only for eligible human-Trainer encounters.
- Return the player to the last activated healing location.
- Fully heal the player's party there.
- Keep earned EXP, levels, move-learning queues, evolution queues, captures,
  and Pokedex progress.
- Keep legally consumed items consumed.
- Do not mark the opponent, quest, or story objective complete.
- The battle reports the result; the overworld system handles transport and
  healing.

Special partner victory rule:

- If all player Pokemon faint but the partner still wins, the result is Team
  Victory and normal victory rewards are granted.
- Do not transport the player.
- Revive the first valid player Pokemon in party order to 1 HP and cure its
  major status. Leave the other fainted player Pokemon fainted.

### Battle Style and Switching

- Support Shift and Set styles.
- Shift is the Casual default for eligible Single Battles.
- Double, Multi, and partner battles use Set behavior because the free Shift
  prompt is not applicable there.
- Voluntary switching consumes that Pokemon's action.
- Trapping, pivoting moves, Pursuit-like rules if added, and other switch
  interactions follow the chosen official baseline.
- Mandatory faint replacements do not consume the replacement's next turn.
- In doubles, remaining queued actions finish before required replacements are
  selected.
- Both active Pokemon may choose to switch, but they cannot choose the same
  reserve.
- If both active Pokemon faint and only one reserve remains, it enters the left
  slot.
- Eggs are invalid for battle, EXP, and usable-party calculations.
- Standard obedience rules are supported.
- Friendship and evolution requirements remain, but modern friendship combat
  bonuses are disabled.

### Turn Selection and Resolution

- The player chooses actions first.
- Enemy AI chooses without reading the player's unexecuted choices.
- Determine action order from move priority, Speed, and the documented tie
  rules.
- Resolve actions one at a time, including immediate consequences.
- Process fainting and battle-end checks at deterministic points.
- Process end-of-turn effects in a visible, ordered trigger sequence.
- Request mandatory replacements before the next normal turn.
- Illegal commands return to selection without consuming the turn.
- A legal but failed escape attempt consumes the action.

Exact priority-and-Speed tie rule:

- A tie between Pokemon on different sides favors the player side.
- If all four active Pokemon tie, both player-side Pokemon act before both
  opponent-side Pokemon.
- Ties between Pokemon on the same side are random.
- An AI-controlled partner still belongs to the player side for cross-side tie
  priority.

Target resolution:

- If a selected single-target opponent faints before the move resolves,
  redirect the move to the other living opponent when one exists.
- If the selected target was captured, cancel queued moves aimed at it. Do not
  redirect those moves and do not consume their PP.
- A Pokemon using Fly, Dig, or a similar semi-invulnerable move remains
  selectable.
- If it is still unreachable when the attack resolves, the attack normally
  misses unless an official exception applies.
- If it has returned before resolution, the attack can hit normally.
- Do not grey out the semi-invulnerable target merely because it may be
  unreachable at resolution.
- Empty or fainted battlefield positions cannot be selected.
- Spread moves such as Earthquake affect allies and opponents according to
  official targeting rules.
- The player chooses Struggle's target in Double Battles.

## Partner Battles

The Settings menu provides three partner-control modes:

- **AI Controls Partner** -- default.
- **Player Controls Both**.
- **Ask Before Each Partner Battle**.

The setting is chosen outside battle and cannot change midbattle. A battle
captures a settings snapshot when it begins.

- Player and partner are separate Trainers with separate predetermined parties
  and separate Bags regardless of who chooses commands.
- Changing control mode changes only the decision-maker.
- The player cannot edit a partner's species, stats, level, moves, Ability,
  held item, or party lineup.
- When controlling the partner, the player may command legal switches among
  that partner's predetermined party.
- Each partner has a manually authored team that may change with story
  progression. Team design is deferred to character design.
- Partner AI chooses actions only for its own active Pokemon.
- A partner item may target only the partner's own party unless an item
  explicitly follows a different official targeting rule.
- The player Bag similarly belongs to the player side's party.
- Each Trainer may use one Bag action in a turn, so a partner battle can contain
  at most one player item action and one partner item action that turn.
- Partner AI may see the player's selected action for coordination. Enemy AI
  may not.
- Partner AI treats player and partner Pokemon equally when evaluating legal
  ally-support targets.
- If the partner has no usable Pokemon, its slot stays empty; the player cannot
  fill it with a second player Pokemon.
- Partner Pokemon cannot be used for capture actions.

## Enemy and Partner AI

Use reusable AI profiles rather than a single omniscient system:

- Wild.
- Basic Trainer.
- Skilled Trainer.
- Boss.
- Tutorial/scripted.
- Partner.

General rules:

- AI may use public and revealed information: visible Pokemon, HP, status,
  revealed moves, revealed Abilities, prior actions, type information, and
  normal battle-state calculations.
- AI cannot read the player's current unexecuted choice.
- AI may calculate damage ranges, type matchups, likely knockouts, accuracy,
  priority, and Speed from information its profile is allowed to know.
- Weaker profiles choose among reasonable actions with greater variation.
- Boss profiles choose stronger actions with limited variation but still obey
  the normal battle rules.
- Trainer AI may switch and use a finite, explicitly configured Bag.
- Ordinary Trainers cannot use Revives.
- A specific boss may use Revives only when explicitly designed and balanced
  around them.
- Wild Pokemon do not have Trainer Bags or ordinary party switching.
- Only explicitly configured wild species or encounters may flee.
- Bosses do not cheat unless a scripted exception is explicitly authored and
  communicated to the player.
- Only Casual AI is in current scope.
- The placeholder opponent controller simply returns Vine Whip through the
  reusable action-selection interface; it is not a strategic AI milestone.

## Escaping

- Trainer battles normally block Run with a clear message and consume neither
  item nor turn for the blocked command.
- Wild escape will use a custom probability based on player Pokemon Speed and
  level versus the encountered Pokemon's Speed and level.
- The user will provide the exact escape formula later. Do not invent it.
- A legally attempted but failed wild escape consumes the action.

## Capture and Wild Reinforcements

- A Pokeball may be used while multiple wild Pokemon remain active.
- The player selects one wild target and throws at most one ball per Trainer
  action.
- Throwing a ball consumes the action.
- A failed capture consumes the ball and action.
- A successful capture removes only the selected Pokemon. Battle continues if
  another wild Pokemon remains.
- Multiple wild Pokemon may be captured over multiple turns.
- Partner Trainers cannot capture.
- Use the standard Scarlet/Violet capture calculation.
- If the party and storage are both full, block the Pokeball before it is
  thrown, show a clear message, and consume neither item nor action.
- A captured Pokemon is pending until the battle ends and cannot join that same
  battle.
- At battle end it goes to the player's party if space exists; otherwise it
  goes to storage.
- It retains its captured HP, status, PP, and original held item.
- Capture grants the same EXP and standard EV yield as defeating that Pokemon.
- If the last wild opponent is captured, the main outcome is Victory with a
  capture cause, because no wild opponents remain.
- Captures already completed persist even if the player later escapes or loses
  the remaining encounter.

When a target is captured:

- Cancel that target's queued action.
- Cancel other queued moves aimed specifically at it.
- Do not redirect those canceled moves.
- Do not consume PP for a canceled move that never executes.

Cry for Help rules:

- It can fill the second active wild slot only.
- It has an 80% success chance when an empty slot exists.
- A failed attempt still consumes the caller's action and PP.
- Only one successful reinforcement is allowed per entire battle.
- After a success, Cry for Help becomes unavailable for the rest of that battle.
- A summoned Pokemon begins acting on the next turn.
- Summoned wild Pokemon may also be captured.
- Formulas for third or fourth reinforcements are future work and must not be
  designed now.

## EXP, EVs, Levels, Moves, and Evolution

- Use the modern Gen VII-and-later scaled EXP formula used by
  Scarlet/Violet. The exact public formula will need a trustworthy mechanics
  reference because official public pages do not publish it.
- Settings provide Party-Wide EXP and Participant-Only EXP.
- Party-Wide is the default.
- The selected distribution setting is snapshotted when battle begins.
- Track participation separately for every defeated or captured opponent.
- A Pokemon participates against an opponent when it has been sent onto the
  field against that opponent before the opponent leaves battle. It does not
  need to attack or spend a complete turn.
- A Shift replacement entering only after the defeated opponent has left does
  not qualify against that opponent.
- Fainted Pokemon receive no EXP or EVs except for the explicit same-resolution
  exception below.
- In Party-Wide mode, eligible participants receive the full calculated share;
  eligible nonparticipants receive half.
- In Participant-Only mode, only eligible participants receive EXP.
- Every non-fainted player Pokemon eligible for EXP receives the opponent's
  full EV yield under modern rules, including level-100 Pokemon.
- NPC partner Pokemon receive no persistent EXP or EVs.
- A partner landing the final hit does not prevent eligible player Pokemon from
  receiving EXP or EVs.
- If a Pokemon causes the knockout or capture and then faints during the same
  action resolution from recoil or a linked effect, it remains eligible for
  that opponent's EXP.
- Apply EXP immediately after each opponent leaves battle.
- Immediate level increases and stat changes affect later turns in the same
  battle, but do not recalculate an action order already locked for the current
  turn.
- When Max HP rises from a level, add the same increase to current HP.
- Level 100 stops stored EXP gain but still allows EV gain.
- The UI may show the theoretical EXP with a clear MAX indication for a
  level-100 Pokemon.
- When one move defeats two opponents, combine the EXP presentation cleanly
  rather than duplicating confusing sequences.
- Show exact EXP for eligible participants, including switched-out
  participants, and a summary that the rest of the eligible team also gained
  EXP where appropriate.
- Queue move learning and evolution until the entire battle concludes.
- Evolution occurs after Victory, Defeat, or Escape, never during battle.
- Process queued evolutions in party order.
- Normal evolution cancellation is allowed; an explicitly scripted evolution
  may define otherwise later.

## Money

Money cap: **$999,999**.

Normal Trainer victory:

- Base reward is `floor(2% of the pre-reward balance)`.
- Minimum reward is $1.
- Rematch victories add one percentage point for that individual Trainer.
- Initial victory: 2%.
- First rematch victory: 3%.
- Second rematch victory: 4%.
- Third and every later rematch victory: 5%.
- A loss or escape does not increase the rematch counter.

Boss victory:

- Base reward is 5% of the pre-reward balance.
- Ordinary boss rematches are not allowed.
- Elite Four and League Champion rematches are allowed in Casual but always
  remain at 5%; they receive no rematch increase.

Amulet Coin:

- Follow the official modern effect: double the final prize when a holder
  participated.
- Multiple Amulet Coins do not stack.
- The earlier custom percentage-addition formula was rejected in favor of this
  official behavior.

Reward calculation order:

- Read the pre-reward balance.
- Determine the encounter's percentage rate.
- Round down.
- Apply the required $1 minimum.
- Apply legal reward modifiers such as Amulet Coin.
- Clamp to the money cap.
- At $0, a normal win therefore grants at least $1, or $2 with a valid Amulet
  Coin.
- At the cap, skip reward calculation and the money-received message.
- Near the cap, show only the amount actually added; discard excess.

Defeat loss:

- Apply only to eligible human-controlled Trainer, boss, and scripted human
  encounters.
- Do not apply to wild Pokemon or wild bosses.
- Loss is `floor(3% of current money)` with a $1 minimum when the player has
  money.
- Money cannot fall below $0.
- At $0, no money is lost.
- Escape causes no money penalty.
- Show only the amount gained or lost, not the underlying percentage formula.
- Check the cap or floor during result resolution; do not build a continuous
  money-monitoring system.

Reward sequence:

- Award opponent EXP immediately when that opponent leaves.
- Resolve postbattle money when the battle result is known.
- Process queued move learning and evolution after battle resolution.

## Bag, Battle Items, and Held Items

- Full command menu: Fight, Bag, Pokemon, Run.
- The initial placeholder exposes only Attack and shows the other three as
  disabled placeholders.
- Battle Bag shows usable battle categories. Hide unrelated Key Items unless a
  specific battle explicitly enables one.
- Hyper Potion heals 120 HP.
- It can target a non-fainted Pokemon in the owner's party, including a reserve.
- It cannot target a full-HP Pokemon.
- Healing is capped at Max HP.
- A valid use consumes the item and action.
- If the command is blocked before use, consume neither item nor action.
- If the item is legally used but its effect is then prevented, consume it and
  the action according to official behavior.
- Player Revives are allowed when owned and legal.
- Ordinary enemy Trainers cannot use Revives; explicitly configured bosses may.
- Status-healing items may target active Pokemon or reserves in the owner's
  party.
- X items target the user's active Pokemon and use official stat-stage rules.
- Encounter-specific Bag restrictions must be visible and explained.

Held-item persistence:

- A Berry or other item consumed normally remains consumed after battle.
- An item restored by Recycle remains restored.
- Temporary ownership changes from Knock Off, suppression, Trick, Switcheroo,
  Thief, and similar battle effects reset to original ownership after battle.
- A captured wild Pokemon keeps its original held item.
- Battle-generated items always disappear after battle.
- The player cannot permanently equip or transfer held items from the Bag
  during battle.

## Input Contract

Supported gameplay input is keyboard and controllers connected to the Windows
PC. Controller support does not mean console releases. Mouse and touch gameplay
are not supported.

Prompts follow the most recently used supported input device, not merely whether
a controller is connected. Recognized controllers show matching glyphs;
unknown controllers use generic gamepad prompts.

| Action | Keyboard | Xbox | PlayStation | Nintendo |
| --- | --- | --- | --- | --- |
| Move/navigation | Arrow keys | D-pad/left stick | D-pad/left stick | D-pad/left stick |
| Confirm/interact | C | A | Cross | A |
| Cancel/back/menu | X | B | Circle | B |
| Sprint | Z | X | Square | Y |
| Registered Items | D | Menu | Options | Plus |
| Quick Pokeball list | V | Y | Triangle | X |

Additional rules:

- The most recently pressed direction wins when keyboard or D-pad directions
  conflict.
- Analog movement is quantized to one cardinal direction; no diagonal movement.
- In Auto Sprint mode, Sprint does nothing.
- Cancel goes back when an interaction or menu is active; otherwise it opens
  the field menu.
- The present battle prototype needs only navigation, Confirm, and Cancel.
- Overworld movement, Sprint, main menu, Bag, and Registered Items are later
  scope.
- Automatic Key Item registration belongs in a future Bag/Registered Items
  design, not technical preferences.
- The quick Pokeball shortcut is available only in wild battles.
- It opens a list showing ball type, quantity, and a short description.
- It remembers the last selected ball as normal focus when reopened, without a
  special highlight or badge.
- If no Pokeballs exist, the shortcut is unavailable with a clear reason.
- The same ball-selection flow remains available through the Bag.
- Only one ball can be thrown per Trainer action.
- In a multi-wild battle, the player chooses the capture target.

## HUD, Menus, and Text

Player Pokemon panel:

- Name, level, gender, HP bar, exact current/max HP, major status, and EXP bar.

Opponent Pokemon panel:

- Name, level, gender, HP bar, and major status.
- Do not show exact current/max HP or an EXP bar.

Party strip:

- Six stable party positions.
- Healthy usable Pokemon: standard filled red Pokeball.
- Fainted Pokemon: greyed filled Pokeball.
- Empty slot: small hollow grey Pokeball.
- Major status changes the whole ball to a muted status color and adds a more
  vivid, dominant status symbol.
- Paralysis: muted/dark yellow ball with a vivid lightning symbol.
- Poison: muted purple ball with one vivid skull.
- Badly Poisoned: deeper purple ball with one large skull and two smaller skulls
  at the lower left and lower right.
- Burn uses muted burnt orange with a bright flame; Sleep uses muted navy with
  a bright `Z`; Freeze uses muted icy blue with a bright snowflake.
- Fainting overrides status presentation. The party strip shows only the
  fainted state, and prior major status and battle-only effects are cleared.
- Badly Poisoned uses one combined three-skull icon. All status symbols and
  colors must be validated at the actual party-strip size and may not rely on
  color alone.

Command and move UI:

- Fight, Bag, Pokemon, and Run use a directional 2x2 command layout.
- The move layout is also navigable by direction, Confirm, and Cancel.
- The main move tile shows type color/symbol, move name, effectiveness, and
  current/max PP.
- Do not show a written type name; type is communicated by color and box/icon
  design.
- Holding Confirm on a move opens details showing only Category, Power,
  Accuracy, and a short description.
- Do not show an exact damage prediction.
- A quick Confirm tap submits the selected move or selected target. There is no
  second confirmation tap.
- A long Confirm hold opens details; releasing after details must not submit the
  move.
- Once the player chooses a complete valid action and target, lock it
  immediately.
- Directional focus must remain clearly visible before submission.
- Bullet Punch's Power 40 is formula input, not a promise to remove exactly 40
  HP.
- Known neutral damage matchups show `Effective`.
- Unknown species show no effectiveness label.
- Effectiveness knowledge is snapshotted when battle begins. An unknown species
  stays blank for that entire first battle, then becomes Seen for future
  battles even after Escape or Defeat.
- Status moves use `-` where damage effectiveness does not apply. Known neutral
  damaging matchups show `Effective`; a truly unknown matchup stays blank.
- Double Battle and spread-move effectiveness labels follow the final rules in
  questions 314-316 below.

Resolution presentation:

- Hide the command menu while actions resolve.
- Show the battle text box while keeping relevant HP panels visible.
- Do not show floating damage numbers.
- HP and EXP bars animate smoothly.
- Only Cancel may complete an HP or EXP bar animation instantly without changing
  the underlying result. Confirm does not skip either bar animation.

Text controls:

- Confirm does nothing while text is still revealing.
- Once the line is complete, Confirm advances.
- The first Cancel press instantly completes the current line but does not
  advance it.
- A second Cancel press advances.
- Holding Cancel rapidly reveals and advances messages.
- Fast-forward always stops at player choices or other required decisions.

Settings:

- Text Speed: Slow, Normal, Fast.
- Battle Animation Speed: Normal, Fast.
- Battle Animations: On, Off.
- Battle Animations Off removes move-specific VFX, move-specific sounds, and
  move-specific camera shots.
- It keeps Pokemon attack animations, target-hit animations, HP/status changes,
  battle text, switch/faint presentation, cries, and results.
- The player may skip eligible animations with Confirm or Cancel, except HP and
  EXP bar animations, which use Cancel only.
- Story cinematics are controlled separately from Battle Animations.

## Camera, Animation, and Audio Presentation

Camera:

- Use one stable standard command view.
- After roughly five seconds of no input, the final system may enter an idle
  camera sequence.
- Each idle view lasts roughly five to eight seconds with slight variation.
- Avoid immediately repeating the same curated angle.
- Commands remain visible and usable while an idle angle is active.
- Any directional or button input instantly cuts back to the standard command
  view before processing the input.
- Every idle-camera change and every return uses an instant cut, not a blend.
- Use curated authored angles, not free orbit.
- Restrict cameras to angles compatible with available front and back 2D sprite
  animation sets. Do not reveal unsupported sprite sides.
- Each arena may override camera presets while reusable defaults remain
  available.
- A Dynamic Battle Camera setting may enable or disable idle/move-specific
  camera behavior.
- Battle Animations Off skips move-specific camera shots but retains standard
  Pokemon attack and target-hit presentation.
- The one-week placeholder uses one fixed camera. Dynamic camera work belongs
  to the later polished layer.

Pokemon animation states already accepted for the eventual system:

- Idle.
- Send-out/entrance.
- Generic Physical action.
- Generic Special action.
- Generic Status action.
- Hit reaction.
- Faint.
- Recall/switch.
- Major-status reaction.
- Victory.
- Front and back variants where the supported camera arcs require them.

Trainer presentation already accepted:

- Entrance.
- Idle.
- Pokeball throw/send-out.
- Item use.
- Command/order gesture.
- Switch/recall reaction.
- Battle reaction.
- Victory.
- Defeat.

Audio and effects:

- Pokemon cries occur on send-out and faint, with optional authored reactions.
- Pokemon attack sounds, hit sounds, and cries remain when Battle Animations are
  Off.
- Move-specific sound effects are removed when Battle Animations are Off.
- Use authored ground contacts and shadows for 2D sprites in the 3D arena.
- Reusable generic action animations and move presentations are allowed.
- Signature or showcase moves may override generic presentation.

Missing assets:

- If a Pokemon or move lacks required presentation assets, it is not eligible
  to be considered finished game content.
- During development, generic fallbacks may be used as placeholders.
- Release validation blocks content from being marked complete while it uses
  intentional placeholders. A packaged emergency runtime fallback remains
  available to prevent a crash if an expected presentation asset is missing.

## Effectiveness Knowledge Evidence

The previous session did not find an official Scarlet/Violet source that states
the exact move-menu effectiveness unlock timing. The official Pokemon glossary
confirms only that the Pokedex records Pokemon a Trainer has seen or caught.
Non-official gameplay references report that a prior encounter/Seen entry is
enough and capture is not required.

Do not claim the exact official trigger is proven. Solarus deliberately uses
the battle-start snapshot and post-battle Seen rule recorded above; this is a
project decision, not proof of Scarlet/Violet's exact trigger.

References:

- `https://www.pokemon.com/uk/play-pokemon/about/video-game-glossary`
- `https://pokemondb.net/pokebase/373478/sword-shield-sometimes-indicate-effectiveness-moves-battles`

## Final Interview Decisions: Questions 311-394

Questions 311-323 are retained below for traceability. They are answered and
are not a continuation questionnaire. All recommendations were accepted, with
the final clarifications recorded after question 323.

**311.** Should an unknown species remain blank for the entire first battle,
then become Seen for all future battles even if the player ran away?

Recommendation: yes. Take the known-species snapshot when battle begins, keep
the first encounter unknown for that battle, then preserve Seen afterward even
after Escape or Defeat.

**312.** Should effectiveness knowledge be form-specific when a form changes
its typing, while purely cosmetic forms share knowledge?

Recommendation: yes, because knowing one type-changing form should not reveal a
different form's matchup automatically.

**313.** For moves where damage effectiveness does not apply, such as Protect,
should the UI show `-`, while a truly blank field remains reserved for an
unknown opponent?

Recommendation: yes. A known neutral damaging matchup still shows `Effective`.

**314.** In a Double Battle before target selection, should the move tile use
these rules?

- Show the shared label when both living targets have the same known result.
- Show `Varies` when their results differ.
- Show `Varies` when one is known and one is unknown.
- Show blank when both are unknown.

Recommendation: yes, because one label cannot truthfully describe two
different targets.

**315.** During target selection, should each opponent display its own
effectiveness label beneath it, while an unknown opponent displays nothing?

Recommendation: yes.

**316.** For spread moves without a target-selection screen, should individual
effectiveness labels appear under every affected opponent while the move is
focused?

Recommendation: yes; use `Varies` on the main tile when results differ.

**317.** Are these remaining status-ball mappings accepted?

- Burn: muted burnt orange ball with a bright flame.
- Sleep: muted navy ball with a bright `Z`.
- Freeze: muted icy-blue ball with a bright snowflake.

Recommendation: accept them, but validate contrast at the actual party-strip
size and do not rely on color without the symbols.

**318.** When a statused Pokemon faints, should the party strip show only the
fainted appearance and hide the status color/icon?

Recommendation: yes. Fainting is the information needed for team availability;
the prior major status and battle-only effects are cleared rather than retained
for later display.

**319.** Should the Badly Poisoned three-skull mark be one combined icon and be
tested at the actual small party-strip size?

Recommendation: yes. Three separate tiny UI elements are more likely to become
misaligned or unreadable.

**320.** For presentation fallbacks, should moves use three reusable classes:
generic Physical, generic Special, and generic Status?

Recommendation: yes, with optional move-specific overrides.

**321.** Should release validation prevent a Pokemon or move from being marked
complete while it intentionally uses a placeholder, while an emergency runtime
fallback remains packaged to prevent a crash if an asset is unexpectedly
missing?

Recommendation: yes. This preserves the user's quality gate without making a
missing asset crash the game.

**322.** Should every attack presentation define a specific impact moment when
damage, HP animation, hit reaction, and hit sound begin?

Recommendation: yes, so presentation and battle results remain synchronized.

**323.** For multi-hit attacks, should each hit have its own impact, HP
reduction, and hit reaction, followed by the final hit-count message?

Recommendation: yes; this follows normal Pokemon readability.

### Final Clarifications for Questions 311-325

- **311:** Take the known-species snapshot when battle begins. An unknown
  species stays blank for the whole first battle and becomes Seen afterward,
  including after Escape or Defeat. This is a Solarus decision; the exact
  official effectiveness-unlock trigger remains unproven.
- **312:** Effectiveness knowledge is form-specific for type-changing forms;
  cosmetic forms share knowledge.
- **313:** Non-applicable effectiveness shows `-`; blank means unknown; a known
  neutral damaging matchup shows `Effective`.
- **314:** Before target selection in Doubles, show a shared result if both
  living opponents match, `Varies` if results differ or only one is known, and
  blank if both are unknown.
- **315:** During target selection, each opponent shows its own known result;
  an unknown result remains blank.
- **316:** A spread move has no separate target-selection screen. While
  focused, it marks every affected position at once, including an ally when the
  move affects that ally. Each affected Pokemon shows its known effectiveness.
  Confirm submits immediately.
- **317:** Use the accepted Burn, Sleep, and Freeze party-strip mappings above
  and validate their symbols and contrast at actual size.
- **318:** A fainted Pokemon shows only the fainted state. Its prior major
  status and battle-only effects are cleared; do not retain a hidden status for
  later display.
- **319:** Badly Poisoned uses one combined three-skull icon validated at actual
  size.
- **320:** Generic Physical, Special, and Status presentations are the reusable
  fallbacks; individual moves may override them.
- **321:** Release validation blocks intentional placeholders from being called
  complete, while an emergency packaged fallback prevents crashes.
- **322:** Every damaging presentation has a defined impact moment.
  Target hit animation, a brief sprite-only white flash, HP movement, and hit
  audio begin at that moment. Use distinct readable audio strength for neutral,
  Not Very Effective, and Super Effective hits. Misses and immunities do not
  use damage flash or hit audio. Soften repeated flashes for multi-hit moves to
  avoid strobing.
- **323:** Each successful multi-hit impact gets its own HP change and hit
  reaction, followed by the actual final hit-count message.
- **324:** Use the spread-move focus behavior from question 316 and do not add a
  separate friendly-fire warning.
- **325:** Use the complete synchronized hit-feedback package from question
  322 for every successful damaging impact.

### Questions 326-353: Action and Result Presentation

- **326:** Simultaneous spread targets begin hit reactions and HP movement
  together. Any faint presentations follow in a stable order.
- **327:** Each target may receive its own effectiveness cue, mixed so several
  simultaneous cues do not become noisy.
- **328:** An ally damaged by a spread move receives the same damage feedback as
  any other target.
- **329:** With Battle Animations Off, retain generic Pokemon action and hit
  reactions, the brief hit flash, HP changes, and effectiveness audio. Remove
  move-specific VFX, sounds, camera shots, and presentation.
- **330:** A miss shows the attacker's generic action and miss text. The target
  has no damage reaction, flash, or HP change.
- **331:** An immunity may play the move up to its no-effect moment, then shows
  no-effect audio and text without damage feedback.
- **332:** Protect resolves to a clear block moment with block audio and text,
  without damage feedback.
- **333:** Critical hits do not use unique critical-hit audio.
- **334:** Neutral hits do not show a post-hit effectiveness message. Not Very
  Effective, Super Effective, and immunity results do. The move menu may still
  show `Effective` for a known neutral matchup.
- **335:** A successful Status move uses the generic Status action first. At
  the defined effect moment, the move's effect animation and effect audio begin
  together, and the battle state and status icon update in sync. Failure uses
  the correct failure feedback and no false success animation.
- **336:** Healing has a defined effect moment with healing sound, visual, and
  upward HP movement, without a damage reaction.
- **337:** For effects such as draining or recoil, resolve target damage and any
  required faint presentation before the user's healing or recoil, except
  where a verified official move-specific rule requires a different order.
- **338:** End-of-turn damage such as Poison occurs only after a multi-hit move
  has fully resolved; it does not interrupt Bullet Seed between hits.
- **339:** Critical hits use a small visual accent and `A critical hit!` text,
  with no special audio.
- **340:** Multi-hit damage resolves one hit at a time. If a hit reaches zero
  HP, remaining hits stop and the count reports only completed hits.
- **341:** For a damaging move that also causes status, resolve damage and HP
  first, then begin the status animation and audio together before updating the
  icon and message.
- **342:** A damaging move's stat-change feedback follows its damage feedback.
- **343:** When an action produces several visible results, show them
  separately in their actual resolution order.
- **344:** Reveal an Ability or item only when official rules normally make it
  public. Otherwise use generic or no-effect feedback and do not leak hidden
  information.
- **345:** The first public Ability activation identifies the Pokemon and
  Ability; later activations may use a shorter presentation.
- **346:** A publicly triggered held item identifies the item and its effect.
- **347:** A multi-stage stat change uses one magnitude-aware animation and
  message.
- **348:** A stat change blocked by its cap or floor gives clear failure
  feedback and no false change animation.
- **349:** Temporary battlefield effects receive start and end feedback plus a
  compact active indicator.
- **350:** Every visible end-of-turn HP change identifies its cause and finishes
  its HP animation before the next cause resolves.
- **351:** If several Pokemon reach zero HP simultaneously, their HP bars reach
  zero together, followed by separate faint presentations before the result.
- **352:** Tapping Cancel completes the current multi-hit animation. Confirm does
  not skip it. Holding Cancel fast-forwards the remaining hits while preserving
  final HP and the actual hit count.
- **353:** Fast animation speed shortens actions, impacts, status, fainting,
  camera shots, and HP movement. It does not change battle rules, Text Speed,
  music pitch, cries, or sound pitch.

### Questions 354-366: EXP and Post-Battle Sequence

- **354:** Show a compact EXP panel with exact EXP for eligible participants
  and animate their bars together. Summarize non-participant team gains.
- **355:** Tapping Cancel completes the current EXP animation; Confirm does not
  skip it. Holding Cancel fast-forwards it while leaving final numbers visible.
- **356:** A one-level gain shows the new level and compact stat increases.
- **357:** Several levels gained at once use one combined start-to-final level
  presentation and final stat increases.
- **358:** An in-battle level-up is shown briefly before the next queued action
  and may be fast-forwarded.
- **359:** Queue move learning before evolutions.
- **360:** When all four move slots are full, show the four known moves and the
  new move with Replace and Decline. Cancel requires confirmation before it
  declines learning.
- **361:** Process several learned moves in earned order and process Pokemon in
  party order.
- **362:** Cancelling a normal evolution leaves the Pokemon unchanged,
  continues the queue, and does not ask again during that same sequence.
- **363:** Show each captured Pokemon's party or storage destination.
- **364:** Several captures may use a compact destination summary.
- **365:** Use this overall order: battle outcome, money, capture destinations,
  move learning, evolutions, then overworld. Immediate battle EXP remains at
  its normal battle timing before the post-battle sequence.
- **366:** Use concise messages rather than a separate detailed results screen.

### Questions 367-387: Accessibility, Focus, Audio, and Exit

- **367:** Hit Flash has Normal, Reduced, and Off. Reduced uses a softer tint or
  outline while keeping hit reaction and audio.
- **368:** Camera Shake has Normal, Reduced, and Off and is never required to
  understand a result.
- **369:** Controller vibration has On and Off and defaults to On.
- **370:** Losing application focus does not pause or freeze the battle.
  Current animation and text reveal continue until the next required player
  input, where the game waits indefinitely.
- **371:** Controller disconnection does not pause the battle.
- **372:** Fast animation preserves normal music, cry, and sound pitch.
- **373:** Do not show a Solarus-specific controller-disconnection notice. The
  battle continues until it requires input; another controller or the keyboard
  may take over.
- **374:** Ignore gameplay input while the application is unfocused so a
  background keypress cannot select an action.
- **375:** Refocusing resumes the current state without a pause screen. Prompts
  change when the next supported input is received.
- **376:** Confirm or Cancel may skip an ordinary wild or Trainer introduction
  to the first decision. Required tutorial or story dialogue is not skipped by
  that shortcut.
- **377:** Battle Animations Off retains a short standard Trainer entrance plus
  Pokemon send-out, throw, and cry presentation.
- **378:** Fast animation speed shortens ordinary battle introductions and
  Trainer entrances.
- **379:** If a story cinematic already introduced the opponent and battle
  start, do not replay the ordinary Trainer entrance.
- **380:** Audio automatically mutes while the application is unfocused. This
  is the fixed default, not a player setting.
- **381:** Outside battle, provide Master, Music, SFX, and UI audio controls.
  Pokemon cries belong to SFX unless a later audio design changes that rule.
- **382:** Battle music continues through menus and idle-camera states.
- **383:** Victory music begins only after required damage, faint, capture, and
  trigger processing confirms victory, and before money or progression.
- **384:** Capturing the final wild Pokemon uses the capture-success jingle in
  place of the ordinary victory opening, then proceeds normally.
- **385:** Defeat and Escape use their own result cues and do not use victory
  music.
- **386:** Settings cannot be opened or changed during battle. Each battle uses
  the settings snapshot captured when it began.
- **387:** Closing the application for any reason during battle abandons that
  battle. There is no mid-battle save or resume. The next launch loads the
  latest valid save. Exact manual-save and autosave timing belongs to the later
  save-system design. The resulting ability to quit to avoid a Casual defeat is
  accepted for the present scope; stricter Nuzlocke or Champion rules remain
  future work.

### Questions 388-394: Battle Info and Interview Closure

- **388:** During battle, `D` / Menu / Options / Plus opens the read-only Battle
  Info view. The same action remains Registered Items outside battle. The Quick
  Pokeball shortcut keeps its separate `V` / Y / Triangle / X mapping.
- **389:** Battle Info is available from command, move, and target selection and
  returns to the exact prior selection and focus. Opening it costs no action.
- **390:** The initial yes to a battle-message history was superseded by the
  final answer to question 392.
- **391:** Do not list revealed opponent moves, Abilities, or held items. Keep
  Battle Info simple and limit it to current active Pokemon stat stages,
  weather and field-effect durations, and side effects and entry hazards.
- **392:** Final decision: no battle-message history. Show only the three
  information groups in question 391. Visual layout is deferred for later
  design.
- **393:** Stat-stage information covers both active Pokemon in Singles and all
  four active Pokemon in Doubles. Entry hazards show their active layers where
  applicable; temporary side and field effects show remaining duration where
  official rules define one.
- **394:** The user explicitly said the reusable battle requirements are
  sufficiently defined and closed the interview.

## Intentional Deferrals and Evidence Limits

- The user will provide the exact custom wild-escape formula later. Do not
  invent it. Wild escape cannot be implemented as final behavior until that
  formula is supplied.
- Battle Info's visual layout will be designed later.
- Third and fourth wild-reinforcement formulas remain future work and are not a
  present promise.
- The exact official effectiveness-label unlock trigger is not proven. Preserve
  the explicit Solarus battle-start snapshot rule without presenting it as an
  official discovery.
- Autosave and manual-save timing belongs to the later save-system design.

## What Happens After the Interview

The interview is complete at question 394. Do not ask question 395 or continue
collecting battle requirements unless the user explicitly reopens the topic.

Before writing a formal Battle System GDD:

- Read this completed handoff in full and treat it as the requirements source.
- Do not reopen settled questions merely to fit a generic workflow.
- Identify prerequisite files and any mismatch with the selected authoring
  skill before starting. At this update, `design/gdd/game-concept.md` and
  `design/gdd/systems-index.md` do not exist, while the current `design-system`
  skill requires both.
- Summarize the proposed document structure, identify real unresolved items,
  state the exact destination path, and obtain the write approvals required by
  `CLAUDE.md` and the selected skill.

The game concept, art bible, systems map, estimate, architecture, Unreal
project, and implementation remain separate workflows. Finishing this
interview does not authorize any of them automatically.
