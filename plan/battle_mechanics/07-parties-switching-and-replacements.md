# C06 — Parties, Switching, and Replacements

Priority: P1  
Status: C06 complete under focused validation
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

## C06A Completion Record

C06A completed on 2026-08-21 from clean baseline `c1d478d`. The live roadmap's
dependency status overrode this package's stale `Blocked by C05C` label before
implementation began.

Implemented scope:

- `FBattleSwitchResolver` owns canonical reserve legality, typed blockers,
  typed transfer policy, explicit reserve selection, and the exact one-draw
  forced-switch rule.
- `FBattleEngine::ExecuteCurrentSwitch()` revalidates a committed voluntary
  switch, preserves the structural active slot, clears ordinary transient
  state, and completes or safely cancels the already-spent action.
- Distinct allied reserves execute through the existing deterministic queue.
- Forced move effects resolve without a selector request. Reached pivot effects
  publish one `PivotSwitch` request only while a valid choice is required, then
  resume the same action without another action cost.
- The frozen transition is `LeftActiveSlot`,
  `SwitchTransientStateCleared`, `EnteredActiveSlot`, `Switched`, then
  `ActionCompleted`, following the action's earlier `ActionStarted` event.
- C06B replacement/Shift/Set behavior, hazards, condition mechanics, Ability
  and item execution, named move data, UI, assets, and configuration remain out
  of scope.

Final forced-unity Editor build command:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' PokemonSolarusEditor Win64 Development 'D:\Python\Projects\Pokemon Solarus\Game\PokemonSolarus.uproject' -WaitMutex -ForceUnity -DisableAdaptiveUnity -NoUBA
```

The command succeeded with exit code `0`. Its log is
`Game/Saved/Automation/C06A-Switching-Final2-20260821T101747Z/build.log`.

Final focused Automation command:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\Python\Projects\Pokemon Solarus\Game\PokemonSolarus.uproject' -Unattended -NoSplash -NullRHI -NoSound -NoP4 '-ExecCmds=Automation RunTests PokemonSolarus.Battle.C06A; Quit' '-TestExit=Automation Test Queue Empty' '-ReportOutputPath=D:\Python\Projects\Pokemon Solarus\Game\Saved\Automation\C06A-Switching-Final2-20260821T101747Z\report' '-abslog=D:\Python\Projects\Pokemon Solarus\Game\Saved\Automation\C06A-Switching-Final2-20260821T101747Z\automation.log'
```

The exact `PokemonSolarus.Battle.C06A` filter discovered seven tests. The final
`index.json` records 7 succeeded, 0 succeeded with warnings, 0 failed, 0 not
run, and 0 in process. Every individual result records 0 warnings and 0 errors;
the process exit code was `0`.

- Report:
  `Game/Saved/Automation/C06A-Switching-Final2-20260821T101747Z/report/index.json`
- Editor log:
  `Game/Saved/Automation/C06A-Switching-Final2-20260821T101747Z/automation.log`
- No C05C, earlier package, complete battle, or full project test filter was
  run. No runtime claim is made for those filters.

Final C06A-owned SHA-256 hashes:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleSwitching.h` | `0ae109bab80368304d24af39c23ddedb9d3eb70430ca340f210a65c1a6d917ce` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleSwitching.cpp` | `556b6335e620221f91b66307385355291695d6bcd4fe00821ec9e9a653a36d9b` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleSwitchingTests.cpp` | `af648aa140796af1191a2cad0db84a463ee88b38aec3a271bda32c7c404673e0` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleDecision.h` | `d7c72fd4575691c3a0feb4b2ecff4d6d3321724ee4a91b8f5fd881bf9d3863da` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDecision.cpp` | `97944940987285028c6de791cb7e0e2171618ed4c254607be8fd46e42e7ee437` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEvent.h` | `3ce106a88d296886e2e8a2d760c6f21d7d4923b19b49a007d21a3e567075376f` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEvent.cpp` | `12b29ec706b3fb57f9139247f1660aad90f33ecf0a163ad32503274ffd19285b` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEngine.h` | `0dab42fa09d4f2b219c7e54d7e5cdb4fd13a006372f0f6049baa4703dc938f68` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp` | `71cbec8f068a9745956fa5738c22fea8da6b1e0adfa97f65faa7bfadd6bd1ba5` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.h` | `1aa344a7b77ab2cb543476c87eda4099c6567faf8e4651345591b2c98f205fb4` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEffectExecutor.cpp` | `445fc02baeac9ab58a6a8246ef3472bdc8b49e5d57d99f151b572d0386e47cb3` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.h` | `d0b69b31685efdcb3b5e6cd9eb1132eb515e153978df7dc02607e680f416a689` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp` | `bfb09833d2f313a66ee60fe2f34625b89737057bc3d01acf952fac7fb2c5fec0` |

Protected files retained their pre-write SHA-256 hashes:

| File | SHA-256 |
|---|---|
| `CLAUDE.md` | `5c4e8a530ba03e52788f973c9a99c3e098353304f3ec6852240f3293a15104c8` |
| `plan/battle_mechanics/reference/modern-rules-snapshot.md` | `ded20d707ab67c2bb4d883df8ac07cbd20e38a31766b8a9ef14515c93168ad50` |
| `docs/battle-system-interview-handoff.md` | `476040bcaf0cfdb9d7f97d3fba3ddc752f7760de252bcc7a8775f846a58c98a6` |
| `Game/PokemonSolarus.uproject` | `97d07ae09b7fbcb7e095ebfd5a4a15c1e1c2953e40e93ec2c89bf407257af7e5` |
| `Game/Config/DefaultEngine.ini` | `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920` |
| `Game/Source/PokemonSolarus/PokemonSolarus.Build.cs` | `5055df3ec3790fb34ef6113f2f47ebe1bdafb0cda2e3ae3cd5b5e631a216d8b7` |

Existing test files remained unchanged. No Git commit or other Git write was
performed. C06B's later completion is recorded below.

## C06B Completion Record

C06B completed on 2026-08-21 from clean `main` baseline `7330dda`
(`Implement C06A`). The live roadmap package remained the implementation
contract; `dev-story` was not used.

Implemented scope:

- `FBattleEncounterPolicies` defaults eligible non-Wild Single encounters to
  Shift. Setup canonicalization preserves explicit false and forces Set for
  Wild, Double, and Partner Double formats.
- `EBattleSwitchKind::Replacement` reuses C06A party-slot legality without an
  acting battler, voluntary trapping, an action cost, or RNG.
- Mandatory replacement requests are identified by Trainer plus destination
  active slot. Left/Right owner batches require distinct destinations and
  reserves and are revalidated atomically before occupancy changes.
- The queue-exhaustion checkpoint freezes every replaceable empty slot in
  side/Left/Right order, emits each existing `ReplacementRequired` fact once,
  applies the lone-reserve Left fallback, and presents one owner group at a
  time. Positions without legal reserves stay empty after C05C outcome checks.
- Eligible Shift responses are typed accepts or declines. Accept reuses C06A
  transient cleanup and emits `DecisionAccepted`, `LeftActiveSlot`,
  `SwitchTransientStateCleared`, `EnteredActiveSlot`, `Switched`. Decline emits
  only `DecisionAccepted`, then exposes the opponent replacement request.
- Mandatory replacement emits every `DecisionAccepted` first, then
  `EnteredActiveSlot` and `Switched` per entrant in active-slot order. It adds no
  locked action, action ID, action lifecycle event, or RNG draw.
- Replacement state clears at `EndOfTurn`. No C07A behavior, new phase, event
  enum, replay-schema bump, public engine method, hazard execution, Ability
  execution, item execution, UI, or asset work was added.

Final forced-unity Editor build command:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' PokemonSolarusEditor Win64 Development 'D:\Python\Projects\Pokemon Solarus\Game\PokemonSolarus.uproject' -WaitMutex -ForceUnity -DisableAdaptiveUnity -NoUBA
```

The final command succeeded with exit code `0`.

Final focused Automation command:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' 'D:\Python\Projects\Pokemon Solarus\Game\PokemonSolarus.uproject' -Unattended -NoSplash -NullRHI -NoSound -NoP4 '-ExecCmds=Automation RunTests PokemonSolarus.Battle.C06B; Quit' '-TestExit=Automation Test Queue Empty' '-ReportOutputPath=D:\Python\Projects\Pokemon Solarus\Game\Saved\Automation\C06B-Replacements-20260821T111345Z\report' '-abslog=D:\Python\Projects\Pokemon Solarus\Game\Saved\Automation\C06B-Replacements-20260821T111345Z\automation.log'
```

The exact `PokemonSolarus.Battle.C06B` filter discovered eight tests. The final
`index.json` records 8 succeeded, 0 succeeded with warnings, 0 failed, 0 not
run, and 0 in process. Every individual result records 0 warnings and 0 errors;
the process exit code was `0`.

- Report:
  `Game/Saved/Automation/C06B-Replacements-20260821T111345Z/report/index.json`
- Editor log:
  `Game/Saved/Automation/C06B-Replacements-20260821T111345Z/automation.log`
- The editor emitted one pre-test startup warning that `-ReportOutputPath` is
  now called `-ReportExportPath`. It was command-line startup noise, not a test
  warning; all eight C06B report entries remain warning-free and error-free.
- No C06A, C05C, older battle, complete battle, or full project test filter was
  run. No fresh runtime claim is made for those filters.

Final C06B-owned SHA-256 hashes:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleSetup.h` | `70068fbc3905afe13ebf658317e159ad04b61a91b1dea3955774f210b78210af` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleSetup.cpp` | `d08dc1f1d5213bee75fffe053fc03c8532914b01e9474a9def95dbe414735097` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleDecision.h` | `365cae711aa278cf940712ca731fdfd7444a13c4cd9f9323d0f5d15214b51c99` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDecision.cpp` | `76fde8dc6877da688ca5e0938a2a678e1dcc093dfef650b50175b92eb7dd809e` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleSwitching.h` | `565b9b18292ecab09cae00082a60d54248bdb7707ea090870b999f7f01a068e4` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleSwitching.cpp` | `c0956c27ebc7573123a4c757b687a7bb8da0da673230e4517b369695d534a96f` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.h` | `22d8412b5db593f83a958bf5cb2722a8e0986a522a08eaa91ef13957a16be326` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleState.cpp` | `d31938866830435855ea7d82ec45cf2133b769a3b132c38ded31cbb06059cd04` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp` | `79372d2b600dcdf97e61c0c335bc2c64f93b1a6708d8d4d50dee8397b037003b` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleReplacementTests.cpp` | `a800904e90f071c15aefd1ea4cffa716449e95428c1a81a0abb04a42778fb56a` |

Protected files retained their pre-write SHA-256 hashes:

| File | SHA-256 |
|---|---|
| `CLAUDE.md` | `5c4e8a530ba03e52788f973c9a99c3e098353304f3ec6852240f3293a15104c8` |
| `plan/battle_mechanics/reference/modern-rules-snapshot.md` | `ded20d707ab67c2bb4d883df8ac07cbd20e38a31766b8a9ef14515c93168ad50` |
| `docs/battle-system-interview-handoff.md` | `476040bcaf0cfdb9d7f97d3fba3ddc752f7760de252bcc7a8775f846a58c98a6` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleFaintOutcomeResolver.h` | `60b68d3b9aa5269d56ea2edbc49003382319d9c64881b57a111327e265f98088` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleFaintOutcomeResolver.cpp` | `28844517aca2b8eb39dd3a537f7661de33b885631a62523df03021063b926baa` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEvent.h` | `3ce106a88d296886e2e8a2d760c6f21d7d4923b19b49a007d21a3e567075376f` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEvent.cpp` | `12b29ec706b3fb57f9139247f1660aad90f33ecf0a163ad32503274ffd19285b` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleReplay.h` | `d1cad6df30c07af55aaac1f9442eeb6f8e9fd1d65c0300fc82b71555ad120ef7` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleReplay.cpp` | `b35f1b327c2ab9f12559bd4a55bf50076d952dad4ed4b1f2b2ec50cb9a96cab7` |
| `Game/Source/PokemonSolarus/PokemonSolarus.Build.cs` | `5055df3ec3790fb34ef6113f2f47ebe1bdafb0cda2e3ae3cd5b5e631a216d8b7` |
| `Game/PokemonSolarus.uproject` | `97d07ae09b7fbcb7e095ebfd5a4a15c1e1c2953e40e93ec2c89bf407257af7e5` |
| `Game/Config/DefaultEngine.ini` | `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920` |

Existing tests remained unchanged. No subagent, Git commit, or other Git write
was used. C06 is complete; C07A is the next sequential package and its C06B
dependency is clear.
