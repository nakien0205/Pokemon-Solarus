# C07 — Status, Volatiles, Field, and Side Conditions

Priority: P1/P2  
Status: C07A through C07D complete under focused validation
Required order: Complete

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

### C07A Completion Record

- C07A completed on 2026-08-21 from clean `main` baseline
  `d3d8addce481a9a2e682a0782e5fe226f2b4a6c1`.
- The standalone plain-C++ framework freezes all 17 trigger phases and owns
  deep-copied registrations, deterministic caller-directed ordering, deferred
  phase/effect queues, reentrancy guards, decrement-before-effect duration,
  expiry requests, layers, suppression/restoration, typed cleanup, and ordered
  fact-only lifecycle output. It has no callback or RNG dependency and is not
  wired into `FBattleEngine`.
- C07A added only `BattleTriggerFramework.h`, `BattleTriggerFramework.cpp`, and
  `BattleTriggerFrameworkTests.cpp`. Existing battle runtime, definitions,
  catalog/adapters, event/replay/snapshot contracts, configuration, module
  rules, and tests were not edited.
- The forced-unity `PokemonSolarusEditor Win64 Development` build with
  `-ForceUnity -DisableAdaptiveUnity -NoUBA` succeeded with process exit code
  `0`. The build log is
  `Game/Saved/Automation/C07A-TriggerFramework-20260821T135944Z/build.log`.
- The only runtime filter run was
  `Automation RunTests PokemonSolarus.Battle.C07A`. The exported report records
  exactly 7 succeeded, 0 succeeded with warnings, 0 failed, 0 not run, and 0
  in process; all seven paths use the C07A prefix, every entry has 0 warnings
  and 0 errors, and the process exited `0`. Evidence is under
  `Game/Saved/Automation/C07A-TriggerFramework-20260821T135944Z/`.
- The approved focused-test instruction overrides this roadmap's generic
  full-suite wording for this package. No C06, older battle, complete battle,
  or full-project filter was run, so no fresh runtime claim is made for them.
- Protected pre-existing sources, authorities, configuration, module rules,
  map, and tests retained their pre-write SHA-256 hashes. No `dev-story`,
  subagent, Git commit, or other Git write was used.
- C07B and C07C are next and were not started by C07A. C07D remains later.

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

### C07B Execution Status

- Completed on 2026-08-22 from clean `main` baseline
  `5d1085b7cf2f6797e2d70919ce2f5fd766cc61fd`.
- Added the typed `BattleMajorStatus` rules and integrated only the six
  canonical IDs through C07A and the live battle engine. Existing arbitrary
  `MajorStatus` fixture IDs retain generic storage behavior.
- Application checks mutual exclusion, Fire/Electric/Ice/Poison/Steel immunity,
  and explicit neutral future hooks before status RNG. Sleep, Freeze, and
  Paralysis deny actions before PP; Paralysis modifies post-stage Speed; Burn
  modifies final Physical damage; Freeze target thaw occurs only after a
  reached eligible hit; and ordered residuals process every HP mutation before
  replacement, terminal, or next-turn flow.
- Sleep duration and Toxic stage remain hidden C07A runtime facts. Event types,
  replay schema `4`, and the observer snapshot wire shape remain unchanged.
- The required forced-unity editor build with adaptive unity disabled and UBA
  disabled succeeded with exit code `0`.
- Only `Automation RunTests PokemonSolarus.Battle.C07B` was used for final
  acceptance. `Game/Saved/Automation/C07B-20260822-092726/index.json` records
  exactly nine successes and zero warnings, failures, not-run, or in-process
  entries; every test entry has zero warnings and errors.
- Protected authorities, existing tests, module rules, `.uproject`,
  configuration, assets, and Git history were unchanged. No `dev-story`,
  subagent writing, commit, older battle filter, full battle suite, or
  project-wide test run was used.
- C07C, C07D, C08 behavior, Data Table schemas/assets, UI, and presentation
  remain excluded. Work stopped before C07C.

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

### C07C Execution Status

- Completed on 2026-08-22 from clean `main` baseline
  `0ca58edd5a180e08501adb13b2f84e35db204eb7`.
- Added typed rules and C07A registrations for exactly the 13 approved
  volatiles: Confusion, Flinch, Protect, Leech Seed, Partial Trap, Trap,
  Taunt, Encore, Disable, Substitute, Charging, Recharge, and Fly-style
  semi-invulnerability.
- The live engine now applies the approved selection, before-action,
  reachability, protection, damage-routing, switch-legality, charge-lock,
  faint-target fallback, end-turn residual, duration, and cleanup behavior.
  Protect-breaking disables only the current shield while preserving a
  successful chain; Leech Seed checkpoints target fainting before linked
  healing; and a denied or aborted charged release clears its charge and
  semi-invulnerable state without a second PP cost.
- Added the three required move flags, validated their catalog/adapter masks,
  rejected malformed charge descriptor order, kept Struggle available through
  Encore/Disable deadlocks, and retained last-move/volatile payload facts in
  private runtime state. Snapshot, event, and replay wire contracts remain
  unchanged.
- The required `PokemonSolarusEditor Win64 Development` forced-unity build
  succeeded with `-ForceUnity -DisableAdaptiveUnity -NoUBA` and exit code `0`.
- Only `Automation RunTests PokemonSolarus.Battle.C07C` was run for final
  acceptance. `Game/Saved/Automation/C07C-final-20260822-114857/index.json`
  records exactly 8 succeeded, 0 succeeded with warnings, 0 failed, 0 not run,
  and 0 in process; every test entry has 0 warnings and 0 errors.
- C07D behavior, assets, UI, configuration, module rules, existing tests, the
  B00B snapshot, the Solarus interview handoff, and Git history were not
  modified. No `dev-story`, commit, older battle filter, full battle suite, or
  project-wide test run was used.
- C07C is complete under its approved focused scope. C07D is dependency-clear
  and next; later packages remain blocked or not started.

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

### C07D Completion Record

- C07D completed on 2026-08-22 from `main` baseline
  `011acf8d20aea3d85119dee9a46e6dc592f6c057`. The pre-existing
  `Game/Content/Maps/FoundationMap.umap` modification was not touched.
- `BattleFieldSideConditions` defines exactly the 21 approved weather,
  terrain, hazard, screen, room, and side-condition IDs. The shared C07A
  framework owns their source, owner, duration, layer, expiry, and cleanup
  registrations.
- The live engine applies canonical weather/terrain damage rules, grounded
  rules, ordered Sandstorm and Grassy Terrain residuals, screens, Wonder Room,
  Magic Room suppression, Safeguard/Mist prevention, Tailwind and Trick Room
  ordering, and exact public snapshot duration/layer facts. Neutral hooks remain
  available for later Ability/item packages without implementing C08 content.
- Damage, order, status, item, and switch-in hazard interactions now require an
  emitted shared-trigger request; a suppressed registration cannot act only
  because its condition state remains present.
- Entry hazards run by creation order after voluntary, forced, Pivot, Shift,
  and mandatory-replacement installation. Every HP change reaches the faint
  boundary before a later hazard, and hazard-caused events preserve the
  condition ID and original setter as their source.
- The required forced-unity `PokemonSolarusEditor Win64 Development` build
  succeeded with `-ForceUnity -DisableAdaptiveUnity -NoUBA` and exit code `0`.
  Evidence is
  `Game/Saved/Automation/C07D-final-20260822-060928Z/build.log`.
- Only `Automation RunTests PokemonSolarus.Battle.C07D` was run for final
  acceptance. The exported
  `Game/Saved/Automation/C07D-final-20260822-060928Z/report/index.json`
  records exactly 9 succeeded, 0 failed, 0 not run, and 0 in process; every
  test entry has 0 warnings and 0 errors. The matching process exited `0`.
- Gravity and other excluded field content, concrete Ability/item activation,
  assets, UI, configuration, module rules, existing test files, the B00B
  snapshot, the Solarus interview handoff, and Git history were not modified.
  No `dev-story`, commit, older battle filter, full battle suite, or
  project-wide test run was used.
- C07 is complete under the approved focused-validation scope. C08A is
  dependency-clear and next.

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

## R4B live weather-trigger addendum — 2026-08-29

`BattleFieldSideConditions.cpp` remains the canonical field-trigger registration
owner. R4B added no new trigger phase: it registers Sun at `BeforeHit`,
`BeforeAccuracy`, `BeforeDamage`, then expiry, and Rain at `BeforeAccuracy`,
`BeforeDamage`, then expiry. The existing C07D contract identity now asserts
those exact ordered phase sets. Dispatch uses the current canonical weather;
inactive, suppressed, or unsupported weather remains neutral.

## R6 optional condition-removal addendum — 2026-08-30

R6 did not add a new condition owner or move-specific cleanup group. The C05
executor now resolves the exact catalog condition family and checks its staged
battler, side, or field owner before removal. A present condition uses the
existing C07 cleanup path exactly once. An absent condition is silent only when
the descriptor carries the typed `OptionalIfAbsent` flag; legacy absence,
missing definitions, invalid targets, and cleanup failures remain failures.

The seven focused R6 identities prove the generic executor contract plus Rapid
Spin, Defog, Brick Break, adapter/catalog validation, and atomic rollback.
Rapid Spin proves connected Substitute damage before the approved user
volatile and user-side hazard removals, followed by Speed +1. Defog proves the
target Evasion drop before exactly 14 present side/field removals and a silent
all-absent pass. Brick Break proves its three screen removals before Damage
through Substitute while Protect preserves the screens and HP.

The final serial C07B, C07C, and C07D reports under
`Game/Saved/AutomationReports/R6-OptionalConditionRemoval-Final-20260830-160508`
passed 9/9, 8/8, and 9/9 with zero aggregate or per-test warnings, errors,
failures, not-run, or in-process results.

## C10A remediation lifecycle summary — 2026-08-30

R2 and R3 deliberately did not model Follow Me or Helping Hand as conditions.
They reuse C07's turn/action cleanup boundaries only to expire their private
registrations deterministically. R4B reused the existing field trigger owner
for current-weather dispatch. R6 reused the existing volatile, side, hazard,
screen, room, terrain, and weather cleanup paths for present conditions.

Therefore C07 still has one condition owner and one duration/cleanup model.
The C10A condition JSON adds only the approved identities and families; it
does not create another lifecycle system.
