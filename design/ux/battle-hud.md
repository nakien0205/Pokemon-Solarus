# Battle HUD Design

> **Status**: Approved
> **Author**: phong + Codex
> **Last Updated**: 2026-08-23
> **Template**: Battle HUD UX Spec
> **Scope**: Battle UI only. This document is not the art bible or visual direction for the whole game.

---

## Scope Boundary

This document governs UI displayed while a Battle is active. Its faceted
crystal style applies only to Battle HUD elements and Battle-specific screens.
It does not define or influence the overworld HUD, menus, dialogue, inventory,
settings, or other non-battle UI. Reusing this style outside Battle requires
separate explicit approval.

The measured layout in this document covers the current Single Battle
integration. Its widgets and presentation adapter must remain reusable across
Battle formats, but Double and partner-Double Battles require a separately
approved layout. Implementations must not silently duplicate or squeeze health
panels into these Single Battle measurements.

---

## HUD Philosophy

The Battle HUD must make the current battle state and next required decision
immediately understandable without obscuring the staged battle. In the current
scope, vivid faceted-crystal surfaces are reserved for the four top-level
command buttons. The supplied icy-blue battle text box provides the message
surface. Health panels and all other Battle UI visuals remain undecided and
must not inherit this styling by assumption. Focus must remain unmistakable
without relying on color or continuous animation.

---

## Battle-Only Visual Direction

### Action Surfaces

The directional 2×2 command menu uses four asymmetric gemstone buttons: red
Fight, gold Bag, green Pokémon, and blue Run. Their labels and symbols remain
visible alongside color. The central stone is not a fifth command. These visual
rules do not apply to moves, items, party choices, targets, or other Battle
interactions; those require separate design decisions.

### Information Surfaces

The supplied icy-blue battle text box is the only information-surface visual
currently approved by this specification. Its faceted white-blue frame
surrounds a quiet interior reserved for Battle messages and command prompts.
Text remains a live UI element rather than being baked into the image, and it
must use a clearly contrasting color. Decorative facets remain near the edges
so they do not interfere with reading. Future Battle-only variants may change
the color palette and visual design while preserving these readability
requirements. This subsection does not determine the visual design of health
panels or other Battle information surfaces.

### Battle Text Contract

The current text style uses Roboto Bold through a replaceable Battle text style
reference. Future Battle-only visual variants may replace the font or color
without changing message behavior, but every replacement must pass the same
bounds, minimum-resolution readability, and contrast requirements.

| Property | Specification |
| --- | --- |
| Baseline font size | 32 px at 1920×1080; approximately 21 px at 1280×720 |
| Text color | Deep navy (`#102A43`) |
| Alignment | Top-left |
| Text rectangle | `X 104, Y 44, W 720, H 112` inside the 944×244 plate |
| Line height | 40 px |
| Visible lines | Maximum two |
| Outline or shadow | None |
| Required contrast | At least 4.5:1 throughout the text rectangle |

The selected navy was sampled against the supplied plate inside this text
rectangle. The measured minimum source-pixel contrast was approximately
10.06:1. Final rendered contrast and readability still require actual-size PIE
validation because font rasterization and runtime presentation are not proven
by source-image sampling alone.

Command prompts and unavailable-command reasons appear instantly. Returning
focus to an available command instantly restores the normal command prompt.
Ordered Battle messages reveal at 24 visible characters per second on Slow, 40
on Normal, and 64 on Fast. Reveal speed counts displayed characters rather than
encoded bytes.

Text wraps at word boundaries. A single overlong token may break only at a
visible-character boundary. Text never shrinks, scrolls horizontally,
truncates, or uses an ellipsis. Overflow becomes another ordered message page.
The authoritative Confirm and Cancel text controls remain unchanged.

The English authoring target is at most 40 visible characters for a command
prompt and at most 56 for an unavailable reason or Battle-message page,
including spaces. The layout reserves 40% translation expansion without
reducing font size. A translation that still exceeds the measured bounds uses
another ordered page rather than smaller text.

### Focus and State Language

The focused command uses the Lifted Gem treatment: it rises slightly, enlarges
modestly, gains a bright persistent outer rim, and casts a stronger shadow.
Focus does not rely on button color or continuous animation. The central stone
remains static and purely decorative. It must not react to focus, appear
selectable, or imply that Mega Evolution or Z-Moves are currently available.
Those mechanics are outside the full-game scope and may only be reconsidered
as post-endgame additions after the full game is complete.

Lifted Gem uses the following measurable states. Pixel values are specified at
the 1920×1080 baseline and follow the project's Unreal DPI scaling at lower
supported resolutions.

| State | Scale | Vertical offset | Rim | Added shadow |
| --- | ---: | ---: | --- | --- |
| Available, resting | 1.00 | 0 px | None | None beyond the source image |
| Available, focused | 1.05 | -8 px | 4 px icy white (`#F4FAFF`) | 8 px downward, 40% navy-black |
| Available, pressed | 1.00 | -2 px | 4 px at 80% opacity | 3 px downward, 25% navy-black |
| Unavailable, resting | 1.00 | 0 px | None | None beyond the source image |
| Unavailable, focused | 1.05 | -8 px | Same full focus rim | Same full focus shadow |

Unavailable command images render at 45% brightness without changing their
hue or alpha. Focus movement completes in 80 ms with ease-out timing. Press
compression completes in 50 ms and releases in 70 ms. Confirm has no hold
threshold or long-press behavior, and these animations never delay command
activation. An unavailable command never enters the pressed state.

The command images use one reusable UI-domain master material with one material
instance per command texture. Rim color, rim width, image brightness, and
shadow strength remain exposed parameters so future Battle-only visual variants
can change them without redefining Lifted Gem behavior.

---

## Information Architecture

### Full Information Inventory

The current Battle HUD communicates:

- Player Pokémon name
- Opponent Pokémon name
- Player HP bar
- Opponent HP bar
- Player exact current/maximum HP
- Current Battle message or command prompt
- Fight, Bag, Pokémon, and Run commands
- Current command focus
- Command availability
- An explanation when a command cannot be used

The opponent's exact HP remains hidden. Move details, party information, item
information, target information, status indicators, and other future Battle
information are outside this HUD design and require separate decisions.

### Categorization

| Category | Information |
| --- | --- |
| Must Show | Both Pokémon names, both HP bars, player exact HP, and the current Battle message or prompt |
| Contextual | Fight, Bag, Pokémon, and Run; command focus; and command availability while awaiting a top-level command |
| Contextual | An unavailable-command explanation when the player attempts or focuses an unusable command |
| On Demand | None in the current HUD |
| Hidden | Opponent exact HP |

The command menu disappears while actions resolve. The battle text box and
relevant health panels remain visible.

### Deferred Required HUD Coverage

The following information remains required by the complete Battle HUD. Its
visual design is intentionally outside the current command-menu and text-box
integration. Deferral means "not designed in this pass," not optional or
removed.

| Future HUD area | Retained requirement | Current status |
| --- | --- | --- |
| Player Pokémon panel | Level, gender, major status, and EXP bar in addition to the name and HP already represented | Required; visual design deferred |
| Opponent Pokémon panel | Level, gender, and major status in addition to the name and HP already represented | Required; visual design deferred |
| Party strip | Six stable party positions with healthy, fainted, empty, and major-status states | Required; visual and interaction design deferred |
| Contextual EXP presentation | Exact EXP for eligible participants, concurrent bar animation, and the approved compact summaries and level-up sequence | Required; visual and sequencing design deferred beyond the existing Cancel-only animation rule |

Later UX specifications must extend the shared Battle presentation boundary for
these requirements rather than creating unrelated data paths.

---

## Data Ownership and Update Contract

Battle UI uses one production presentation boundary shared by runtime and
tests. A small `FBattlePresentationAdapter` consumes observer-safe Battle data
and produces validated display-ready state. The adapter initially exposes the
current HUD projection and may add projections for later Battle screens without
changing this ownership rule.

The presentation flow is:

`Battle core -> observer-safe snapshot, decision request, and ordered events -> presentation adapter -> validated Battle UI state -> Battle widgets`

| UI information | Authoritative source | Presentation owner | Update trigger | Missing or invalid behavior |
| --- | --- | --- | --- | --- |
| Active Pokémon identity and display name | Observer-filtered active-slot and battler facts plus the required Battle display-name resolver | Presentation adapter | Initial display, state-version change, switch, faint, or replacement | Required for a visible health panel; fail closed rather than inventing a name |
| Current and maximum HP | Observer-filtered battler facts | Presentation adapter provides targets; health-panel widget owns only the displayed animation value | Initial display and each accepted HP or active-battler change | Maximum HP must be positive and current HP must be in range; otherwise fail closed |
| Command availability | Pending `FBattleDecisionRequest` legal action kinds | Presentation adapter | A new or replaced decision request | May be absent only while the command menu is hidden; absence during command selection is invalid |
| Unavailable-command reason | Typed `FBattleUnavailableDecisionOption` mapped to localized presentation text | Presentation adapter | Focus or Confirm on an unavailable command, or a replaced request | A visible unavailable command requires a typed reason; no generic invented fallback text |
| Command prompt | Acting battler identity, pending request, and localized prompt template | Presentation adapter | Entering or returning to top-level command selection | Required while the command menu is visible; otherwise fail closed |
| Battle message | Ordered events from an accepted `FBattleResolution`, mapped to localized presentation text | Presentation adapter maps events; presentation coordinator owns queue timing | Accepted resolution and advancement of the current line | An empty queue is valid only when no presentable event remains; event order is never rearranged or fabricated |
| Current command focus | Local command-navigation state | Command widget under presentation-coordinator control | Command-menu entry, return from Battle Info, or directional input | May be absent only while the menu is hidden; a new selection phase defaults to Fight |
| HUD phase and command-menu visibility | Snapshot phase, pending-decision state, and message-queue state | Presentation coordinator | State-version, request, or queue-state change | Contradictory phase data stops presentation and command input |

Widgets never read from or mutate the Battle engine. The presentation adapter
performs typed conversion and validation but owns no Battle state and handles no
player input. The presentation coordinator decides when to rebuild and apply a
projection, controls message timing and screen transitions, and submits only
validated player decisions through the separate Battle-control path.

If required data is invalid before initial display, the HUD is not shown and
the error is reported. If a later required update is invalid, presentation and
command submission stop at the last valid state until the error is handled.
The UI never substitutes fake names, `0 / 0` HP, or invented fallback messages.
Expected absence remains valid when the related UI is intentionally hidden.

Battle-message presentation preserves `FBattleResolution` event order and the
authoritative text controls: Confirm does nothing while a line is revealing and
advances only after it is complete; the first Cancel completes the current line,
a second Cancel advances it, and holding Cancel fast-forwards until a required
choice. Cancel-only HP and EXP animation completion remains unchanged.

---

## HUD States by Gameplay Context

| State | Visible HUD behavior |
| --- | --- |
| Loading and validation | The entire HUD remains hidden until required presentation data validates. |
| Battle introduction | The text box appears only for an introduction line. Each health panel appears when its Pokémon enters the field. |
| Top-level command selection | Both health panels, the text box, all four command gems, and the decorative stone are visible. |
| Child selector | The command cluster hides. The separately designed move, Battle Bag, party, or target UI takes its place. |
| Battle Info | The current selector freezes and stops accepting input. The read-only Battle Info view takes the foreground. |
| Action resolution | The command cluster hides. Health panels and ordered Battle messages remain visible. |
| Faint or forced replacement | Commands remain hidden until replacement presentation and any required choice are ready. |
| Story or tutorial interruption | Command input stops. The authored cinematic or tutorial presentation controls HUD visibility. |
| Application loses focus | HUD presentation continues, but gameplay input is ignored until application focus returns. |
| Controller disconnects | The HUD remains unchanged and shows no custom disconnection notice. Another supported controller or the keyboard may take over. |
| Battle outcome | Commands remain hidden. Result messages and relevant health panels remain until Battle exit. |
| Battle exit | The entire Battle HUD is removed. |
| Initial data error | The HUD never appears and the error is reported. |
| Runtime data error | Command input stops and presentation freezes at the last valid state. |

There is no player-opened pause or Settings state during Battle. On application
refocus, prompts change only after the next meaningful supported input, following
the latest-input rule.

---

## Layout Zones

The following measurements form a provisional integration baseline. They are
exact starting values, not final visual acceptance. Position, spacing, and
health-zone sizes may be tuned after actual-size PIE comparisons at both
supported resolutions. Any accepted tuning must be written back into this
specification.

| Element or zone | 1920×1080 baseline bounds |
| --- | --- |
| Safe area | `X 64, Y 64, W 1792, H 952` |
| Opponent health zone | `X 1216, Y 64, W <= 640, H <= 180` |
| Player health zone | `X 64, Y 572, W <= 640, H <= 180` |
| Battle text box | `X 64, Y 772, W 944, H 244` |
| Command focus envelope | `X 1264, Y 698, W 592, H 318` |
| Resting command artwork | `X 1272, Y 714, W 576, H 298` |

Command artwork uses these local bounds inside the resting command area:

| Asset | Local bounds |
| --- | --- |
| Fight | `X 0, Y 0, W 271, H 147` |
| Bag | `X 311, Y 0, W 265, H 148` |
| Pokémon | `X 0, Y 158, W 270, H 140` |
| Run | `X 311, Y 157, W 265, H 141` |
| Decorative stone | `X 226, Y 74, W 123, H 151` |

The player health zone sits 20 px above the text box. The text box and command
focus envelope have a 256 px horizontal gap. The focus envelope reserves enough
space for the approved lift, rim, scale, and shadow without clipping. The four
HUD bounding zones together cover no more than approximately 31.3% of the
baseline viewport.

All measurements scale uniformly to 66.7% at 1280×720. The lower-left health
panel and text box form one bottom-left anchored stack. The command cluster
anchors bottom-right, and the opponent health panel anchors top-right. Hiding
the command cluster never moves or enlarges another HUD element.

Later tuning may change exact position, spacing, and health-zone measurements,
but it must preserve these approved constraints: the quadrant anchors, native
command-image dimensions, non-overlap, reserved focus envelope, uniform
resolution scaling, and non-shifting behavior. This section determines
placement only; it does not determine the health panels' visual design.

---

## Visual Budget

The current Single Battle HUD displays at most eight conceptual elements at
once: two health panels, one Battle text box, four command gems, and one
decorative stone. Their combined bounding zones cover no more than
approximately 31.3% of the baseline viewport.

The command cluster and a child selector never appear simultaneously. Battle
Info replaces the active selector instead of stacking another interactive layer
over it. The reserved focus envelope remains inside the approved budget, and
persistent HUD elements do not cover the central Battle view. Future child
screens, Double Battle layouts, partner-Double layouts, and Battle Info must
define their own combined visual budgets.

---

## HUD Elements

| Element | Category | Content and behavior |
| --- | --- | --- |
| Opponent health panel | Must Show | Opponent name and HP bar. Exact HP remains hidden. Its visual design is undecided. |
| Player health panel | Must Show | Player Pokémon name, HP bar, and exact current/maximum HP. Its visual design is undecided. |
| Battle text box | Must Show | Uses the supplied icy-blue prototype plate. Displays live Battle messages, command prompts, and unavailable-command explanations. |
| Command menu | Contextual | A directional 2×2 group containing Fight, Bag, Pokémon, and Run. Visible only while awaiting a top-level command. |
| Command gem | Contextual | Uses its supplied prototype image while remaining a real focusable UI control. Receives the Lifted Gem focus treatment. |
| Central stone | Decoration | Static, non-focusable, and non-interactive. It communicates no currently available mechanic. |

The current prototype command PNGs retain their baked English labels and
symbols. The existing live `Text_Fight`, `Text_Bag`, `Text_Pokemon`, and
`Text_Run` widgets do not render over those images. Each underlying button
remains a real focusable control with an accessible command name. If command
localization or editable labels enter scope, these textures must be replaced by
label-free plates rather than covering or drawing over their baked labels.

At the 1920×1080 baseline, each resting command image uses its native source
dimensions (265-271 px wide and 140-148 px high). Lifted Gem may enlarge the
focused image to the approved 1.05 scale. At 1280×720 the images scale down
through Unreal DPI scaling. The HUD must not enlarge the resting images beyond
their source resolution; a materially larger layout requires higher-resolution
source art instead of stretching these textures.

The command and text-box textures use the UI texture group, alpha-preserving UI
compression, and no generated mipmaps. These settings and an actual-size PIE
comparison must be verified during integration. The PNG source format is
lossless, but that alone does not prove final in-game sharpness.

The supplied PNGs remain prototype visual plates and the underlying controls
remain reusable UI elements. Future production versions may separate plates,
labels, and symbols into layers, but this specification does not require that
work now.

---

## Dynamic Behaviors

| State or trigger | HUD behavior |
| --- | --- |
| New top-level command selection | The command menu appears instantly. Fight receives initial focus. |
| Return from Battle Info | The command menu restores the exact command that was previously focused. |
| Directional navigation | Focus moves through the 2×2 layout and immediately applies the Lifted Gem treatment. The central stone remains static. |
| Tap Confirm on an available command | The command activates immediately. Its gem briefly compresses and releases as visual feedback. Command gems have no hold interaction. |
| Focus an unavailable command | The gem remains dimmed but receives the full focus outline. The text box explains why it cannot be used. |
| Tap Confirm on an unavailable command | No action, motion, or sound occurs. The text box immediately reaffirms the reason. |
| Action resolution | The command menu hides. The text box presents Battle messages while relevant health panels remain in their established positions. |
| HP change | The affected HP bar animates smoothly toward its authoritative value. Player exact HP follows the displayed value. Only Cancel may complete the bar animation immediately without changing Battle state; Confirm does not skip it. |
| Missing required HUD data | The Battle validates required presentation data before showing the HUD. Invalid data stops HUD initialization and reports an error rather than displaying invented names, HP values, or other misleading placeholders. |
| Next command-selection phase | The menu reappears instantly with Fight focused. |

The move-selection screen separately owns the existing quick-tap-to-submit and
hold-for-details behavior.

### Command Interaction Map

| Player action | Immediate feedback | Outcome or event |
| --- | --- | --- |
| Direction reaches another command | Focus changes immediately and applies Lifted Gem | Local `CommandFocusChanged`; no Battle-state change |
| Direction points outside the grid | Nothing changes | No event |
| Confirm available Fight | Approved compression and release animation | Open Move Selection; no Battle decision is submitted yet |
| Confirm available Bag | Approved compression and release animation | Open Battle Bag; no Battle decision is submitted yet |
| Confirm available Pokémon | Approved compression and release animation | Open Party Selection; no Battle decision is submitted yet |
| Confirm available Run | Approved compression and release animation | Immediately submit the typed Run decision |
| Focus unavailable command | Full focus rim remains and the text box shows its typed reason | Local presentation update only |
| Confirm unavailable command | The text box immediately reaffirms the reason; no motion or sound | No navigation or Battle event |
| `D` or controller Menu | Battle Info appears | Open read-only Battle Info without consuming an action |
| Close Battle Info | Command menu returns | Restore the exact previous command focus |
| Cancel at top level | Nothing changes | No event |

Directional relationships are fixed:

- Fight: Right moves to Bag; Down moves to Pokémon.
- Bag: Left moves to Fight; Down moves to Run.
- Pokémon: Up moves to Fight; Right moves to Run.
- Run: Up moves to Bag; Left moves to Pokémon.
- Every unlisted direction clamps at the current command.
- Diagonal input is ignored.

The command menu introduces no analytics events.

---

## Platform and Input Variants

The Battle HUD uses 1920×1080 as its design baseline and must remain usable at
1280×720. Layout zones use anchors and proportional spacing. Command gems and
the text box scale uniformly rather than stretching independently. Text, focus
outlines, command symbols, and HP information must remain readable at 1280×720.

The target platform is Windows PC. Keyboard is the primary device for HUD
design and testing. Compatible Xbox, PlayStation, and Nintendo controllers are
secondary design targets with full gameplay parity.

| Action | Keyboard | Controller |
| --- | --- | --- |
| Navigate | Arrow keys | D-pad or left stick |
| Confirm | C | Native Confirm button |
| Cancel | X | Native Cancel button |
| Battle Info | D | Menu, Options, or Plus as appropriate for the recognized controller |

Navigation resolves to cardinal directions only; diagonal focus movement is not
used. The HUD does not respond to mouse hover, mouse clicks, or touch. At
runtime, keyboard and controller inputs have equal authority: the latest
meaningful supported input determines the active input method and displayed
prompts. Recognized controllers use their native glyphs, while unknown
controllers use a generic gamepad fallback. A focused command is always visible
before input is accepted.

---

## Tuning Knobs

Player-facing settings are selected outside Battle and snapshotted when Battle
begins:

- Text Speed: Slow at 24, Normal at 40, or Fast at 64 visible characters per
  second.
- Battle Animation Speed may shorten HP movement but never changes its result
  or the Cancel-only completion rule.
- Settings cannot be changed during Battle.

Designer-facing Battle-only parameters are the font style and text color, focus
rim color and width, unavailable brightness, shadow strength, focus and
compression timing, and provisional layout positions and spacing. Designer
changes must preserve the approved interaction behavior, readability,
native-image rule, and Battle-only scope.

---

## Accessibility

The current Battle HUD uses a practical accessibility baseline rather than
claiming formal WCAG compliance.

- Every command is reachable through keyboard and controller navigation.
- The directional focus order follows the visible 2×2 command layout.
- Focus is communicated through lift, scale, a persistent outline, and shadow
  rather than color alone.
- Each command uses a readable label and symbol in addition to its identifying
  color.
- An unavailable command is dimmed, remains focusable, and receives a written
  explanation in the Battle text box.
- Battle text maintains at least 4.5:1 contrast throughout its approved text
  rectangle.
- The combined focus rim and shadow maintain at least 3:1 non-text contrast
  against the surrounding scene at both supported resolutions.
- Roboto Bold remains approximately 21 px at 1280×720.
- Every button retains an accessible command name even while the current baked
  labels are used.
- Text, command identity, focus, and HP information must remain readable at
  1280×720.
- No essential information depends only on animation, audio, or controller
  vibration.
- Focus does not pulse or move continuously.
- The brief confirmation animation does not currently require a global
  reduced-motion setting. If one is introduced later, the compression animation
  may be removed while retaining static focus feedback.
- Screen-reader support and a formal project-wide accessibility tier are not
  defined in the current scope and remain open decisions.

Accessibility must be validated using the assets at their actual in-game size;
the reference image alone is not sufficient evidence.

These are screen-specific practical requirements. They do not claim a formal
project-wide accessibility certification.

---

## Acceptance Criteria

- [ ] After valid presentation data is ready, the HUD appears within 100 ms,
  excluding map loading, without temporary names, fake HP, or placeholder text.
- [ ] Invalid required initial data keeps the HUD hidden, rejects command input,
  and reports an error.
- [ ] The HUD matches its safe bounds at 1920×1080 and scales uniformly without
  clipping at 1280×720.
- [ ] The current Single Battle HUD never exceeds eight conceptual elements or
  approximately 31.3% combined bounding area.
- [ ] Keyboard arrows and controller directional input reach all four commands
  using the approved adjacency map and clamp at every outer edge.
- [ ] Fight opens Move Selection, Bag opens Battle Bag, Pokémon opens Party
  Selection, and available Run submits the typed Run decision.
- [ ] `D` or the controller Menu button opens Battle Info without consuming an
  action, and closing it restores the exact previous focus.
- [ ] Top-level Cancel changes nothing and produces no sound, animation,
  navigation, or Battle event.
- [ ] An unavailable command remains focusable at 45% brightness, receives the
  complete Lifted Gem focus state, and displays its typed reason immediately.
- [ ] Confirming an unavailable command produces no command motion, sound,
  navigation, or Battle event.
- [ ] The text box uses `X 104, Y 44, W 720, H 112`, its two-line limit, Roboto
  Bold sizing, and deep-navy color without touching decorative facets.
- [ ] Slow, Normal, and Fast messages reveal at 24, 40, and 64 visible
  characters per second respectively.
- [ ] Long or translated text creates ordered additional pages without
  shrinking, truncating, scrolling horizontally, or using an ellipsis.
- [ ] Confirm and Cancel follow the approved message, HP-animation, and
  EXP-animation rules.
- [ ] Every command texture and the text-box texture uses the UI texture group,
  preserves alpha with UI compression, and uses no generated mipmaps.
- [ ] At actual size in the FoundationMap Battle Camera, text contrast is at
  least 4.5:1 and the combined focus boundary is at least 3:1 at both supported
  resolutions.
- [ ] Command art remains sharp at rest and during 1.05 focus enlargement at
  both supported resolutions.
- [ ] Commands remain hidden during action resolution, faint or replacement
  presentation, Battle outcome, and presentation errors.
- [ ] Application focus loss and controller disconnection do not select
  commands or show a custom interruption notice.

---

## Open Questions

- Health-panel visual design is intentionally deferred.
- Move, item, party, target-selection, status, and Battle Info UI designs are
  intentionally deferred.
- The supplied crystal assets require final contrast validation against the
  confirmed FoundationMap Battle Camera scene after integration.
- A formal project accessibility tier and screen-reader support remain
  undecided.

---

## Cross-Reference Check

- All requirements found for the currently scoped Battle HUD are represented.
- Lifted Gem is a local interaction pattern for the four Battle command gems.
  It is not added to a project-wide interaction pattern library and must not be
  reused outside this Battle-specific scope by assumption.
- No navigation mismatch remains for the current command layout.
- Missing required presentation data has a defined fail-closed behavior.
- Cancel-only HP- and EXP-animation skipping is aligned with the authoritative
  battle handoff.
- A formal accessibility tier and screen-reader support remain intentionally
  unresolved.
