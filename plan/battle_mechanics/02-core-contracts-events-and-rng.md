# C01 — Core Contracts, Events, and Deterministic RNG

Priority: P0  
Status: Complete — C01A and C01B passed  
Hard dependency: B00B complete (satisfied)  
Required order: C01A, then C01B (satisfied)

## Objective

Freeze the plain-C++ public language used by every battle package before any
mechanic builds mutable state. Model two active positions per side now so
Doubles never requires replacing foundational identifiers.

## Ownership

The C01 sessions own new core contract files under the existing runtime module
and their focused tests. They may move `EBattleMoveCategory` into a shared move
type header while keeping the existing damage-calculator include compatible.

They must not implement damage modifiers, parties, switching, status behavior,
Data Table loading, UI, Actors, progression, AI strategy, or content rows.

## C01A — Identity, Setup, and RNG Contracts

Define strong value types for:

- `FBattleId`, `FTurnId`, `FActionId`, and `FResolutionId`.
- `FTrainerId`, `FBattlerId`, opaque `FSourcePokemonId`, and
  `FDefinitionId`-based species/form, move, condition, Ability, and item IDs.
- `EBattleSide`, `EBattlePosition` (`Left`/`Right`), and `FActiveSlotId`.
- Stable party slots `0..5`; do not use array addresses as identity.
- `FNatureStatModifier`, containing only the resolved boosted/reduced stat
  multipliers needed by the pure stat calculator. Nature IDs/rows remain C02B's
  ownership.

Define enums and immutable setup values for:

- Encounter kind: Wild, Trainer, Rival, Boss/Gym, Tutorial/Scripted.
- Format: Single, Double, PartnerDouble.
- Phase: Setup, Selecting, Locked, Resolving, MandatoryReplacement,
  EndOfTurn, Terminal.
- Commands/actions: Fight, Bag, Switch, Run, configured WildFlee, Replacement,
  ScriptedEnd, Abandon.
- Outcome and cause: InProgress, Victory, Defeat, Escape, ScriptedEnd,
  Abandoned plus ordinary/capture/partner/simultaneous-faint/OpponentFled
  causes.
- Target classes and public visibility levels.

Create `IBattleRandom` with bounded uniform-integer operations. The engine owns
or retains an explicitly lifetime-safe injected instance. Every call returns a
record containing the bound, result, call ordinal, turn/action/resolution IDs,
and rule purpose. No mechanic may call a global random API.

## C01B — Engine, Decision, Snapshot, and Event Contracts

Freeze these public types:

- `FBattleSetup`: frozen settings/catalog reference, Trainer ownership, party
  entries, starting active positions, Bag counts, capture-capacity snapshot,
  knowledge/visibility snapshot, standard-obedience input snapshot, and
  encounter policies.
- `FBattleEngine`: the only mutable-state owner.
- `FBattleDecisionRequest`: who must decide, request kind, legal command/move/
  switch/item/target values, and a state version.
- `FBattleDecision`: typed payload that echoes the request state version.
- `FBattleRejection`: stable reason enum plus involved IDs; never display text.
- `FBattleSnapshot`: immutable public battle facts.
- `FBattleResolution`: before/after state versions, acceptance/rejection, and an
  immutable ordered event list.

Required engine operations:

- Create from a fully validated `FBattleSetup` and immutable catalog.
- Read the current snapshot and pending decision.
- Submit one typed decision.
- Apply a validated between-actions stat refresh after an opponent-removal
  checkpoint.
- Export deterministic replay inputs and the RNG trace.
- Export a versioned `FBattleReplayRecord` through a canonical serializer with
  fixed field order and explicit enum/integer encoding. Never serialize raw
  struct bytes, padding, pointers, or unordered map iteration.

Each event must contain:

- Event ordinal, battle/turn/action/resolution IDs, typed cause, source and
  targets.
- Numeric before/after or delta where applicable.
- Optional simultaneous-group ID and hit index/count.
- Public visibility/reveal metadata.

Minimum event families:

- Decision accepted/rejected; action locked/started/canceled/completed.
- Move, item, switch, Run, capture, and scripted actions.
- PP/item consumption and RNG checks.
- Accuracy, miss, immunity, Protect, critical, effectiveness, damage, healing,
  HP, status, stages, and field effects.
- Entered/left active slot, fainted, captured, escaped, removed, replacement
  required, opponent-removal checkpoint, and battle ended.

## Invariants

- Only `FBattleEngine` mutates battle state.
- A stale decision version is rejected without mutation or RNG.
- Rejected commands consume no action, PP, item, or RNG.
- Every accepted action receives exactly one stable action ID.
- Event ordinals are total and stable, including simultaneous outcomes.
- Presentation strings, animation timing, `FText`, `UObject`, Actor, World,
  Blueprint, UI, audio, and input never enter these contracts.
- Core snapshots expose facts, not hidden opponent information unless the
  visibility snapshot permits it.

## Tests

Create focused tests for:

- ID equality, invalid/default ID rejection, and party/position identity.
- Setup validation for Single, Double, and PartnerDouble slot shapes.
- Deterministic RNG bounds, exact call sequence, and replay equivalence.
- No RNG consumption for rejected decisions.
- Stable event ordering, simultaneous groups, and hit ordinals.
- Snapshot immutability and monotonically increasing state versions.
- Canonical replay serialization is identical for semantically identical input,
  events, RNG, and output regardless of memory layout.
- Stale-decision rejection and terminal-state rejection.
- No dependency on a World, Actor, UObject instance, Blueprint, or Data Table.

## Acceptance

- Both focused C01 suites and the existing calculator suite pass.
- Public contracts compile without including presentation or data-loader code.
- A minimal no-mechanics engine fixture can accept/reject a synthetic decision
  and produce a replayable event trace.
- C02A and C02B can work against the frozen contracts without editing the same
  shared header.

## C01A Completion Handoff

Status date: 2026-08-20  
Successful run ID: `C01A-CoreContracts-20260820T122558Z`  
Next package: C01B in a new session

### Owned files

C01A added only these runtime and focused-test files:

- `Game/Source/PokemonSolarus/Public/Battle/BattleIdentifiers.h`
- `Game/Source/PokemonSolarus/Public/Battle/BattleSetupTypes.h`
- `Game/Source/PokemonSolarus/Public/Battle/BattleRandom.h`
- `Game/Source/PokemonSolarus/Private/Battle/BattleRandom.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleIdentifiersTests.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleSetupTypesTests.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleRandomTests.cpp`

This handoff and `00-roadmap-index.md` are the only status documents updated.
C01A did not edit the existing calculator, module rules, `.uproject`, Unreal
config, B00B snapshot, Solarus interview handoff, or unrelated sprite/importer
work.

### Frozen C01A contracts

- Runtime IDs are strong value types. Numeric IDs wrap non-zero `uint64`
  values; zero/default is invalid. Authored definition IDs reject `NAME_None`
  and expose typed species/form, move, condition, Ability, and item families.
- Party slots are stable values `0..5`. Active-slot identity is an explicit
  validated side plus `Left`/`Right` position, never an array address.
- Encounter, format, phase, action, outcome/cause, target, visibility, and
  nature-stat enums have explicit `uint8` encodings.
- `FNatureStatModifier` is neutral or one distinct boost/reduction pair. Its
  exact multipliers are `11/10`, `9/10`, and `10/10`; it owns no nature row or
  nature definition ID.
- `IBattleRandom` exposes inclusive bounded integer draws and an ordered trace.
  `FSeededBattleRandom` freezes SplitMix64 plus unbiased rejection sampling.
  Each accepted semantic call records minimum, maximum, bound, accepted raw
  value, result, call ordinal, battle/turn/action/resolution IDs, and rule
  purpose. A one-value range consumes one semantic draw. Invalid bounds or
  invalid context reset the output and consume no state or trace entry.
- The new contracts contain no World, Actor, UObject, Blueprint, Data Table,
  presentation, loader, or global-random dependency.

### Validation evidence

- The first Editor build attempt, `C01A-EditorBuild-20260820T121045Z`, stopped
  before compilation because Unreal Live Coding was active. It exited 6 and is
  preserved separately at
  `Game/Saved/Logs/C01A-EditorBuild-20260820T121045Z.log`, SHA-256
  `9d7785071c3381548c0a51826e1086a362fc22ff48e49f65d96537cedc9e2784`.
- After the user closed the Editor, the fresh
  `PokemonSolarusEditor Win64 Development` build succeeded, compiled all four
  new translation units, and reported 0 warning lines and 0 error lines. Log:
  `Game/Saved/Logs/C01A-EditorBuild-20260820T122558Z.log`, SHA-256
  `e32c401fcb0f93b3636193eff31f34b4af5641efba52ded5ec32764198e3fe48`.
- `PokemonSolarus.Battle.CoreContracts` performed exactly seven tests: 7
  succeeded, 0 with warnings, 0 failed, 0 not run, exit code 0. Report:
  `Game/Saved/Automation/C01A-CoreContracts-20260820T122558Z/index.json`,
  SHA-256
  `bba31749d3f89b904defeb3129c3e74702737837cc6e1d890a1790b973420c12`.
  Log: `Game/Saved/Logs/C01A-CoreContracts-20260820T122558Z.log`, SHA-256
  `31cc6a80f3b82804e5c1da69bd3e79fd383989c229a47873f254b87e6724c6e1`.
- `PokemonSolarus.Battle.DamageCalculator` performed exactly four tests: 4
  succeeded, 0 with warnings, 0 failed, 0 not run, exit code 0. Report:
  `Game/Saved/Automation/C01A-DamageCalculator-20260820T122558Z/index.json`,
  SHA-256
  `c0f99e837365adaa777f6492f6cde91cc062a29e4e17e6dd16d03e0edcabbf1f`.
  Log: `Game/Saved/Logs/C01A-DamageCalculator-20260820T122558Z.log`, SHA-256
  `0230801700c99a5c51841b283add44d8e23f6aaceef827892466b77253231df0`.
- Per the user's latest instruction, the full `PokemonSolarus.Battle` suite was
  deliberately not run. This is the explicit narrow-test exception to item 5
  of the roadmap's session completion contract. C01B's focused suite was not
  run because C01B was not implemented.
- Both JSON reports record 0 test warnings and 0 failures. Separate startup
  noise consists of optional profiler/GPU-capture/tablet DLL messages,
  unavailable EOS anti-cheat, one stale-layout migration warning in the
  CoreContracts run, and Unreal's own `UE::UnifiedErrorTest` diagnostics. The
  logs contain no Automation or Solarus battle issue line.

Final C01A source/test hashes:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleIdentifiers.h` | `f8a081ada899b187eda530a4021e2c124cf03901bf26e3d152bba34fffeee077` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleSetupTypes.h` | `ec413d1985fceed91e2bfdbf96bbc51667d1756403ea4a4d0afe594a5b77cbb8` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleRandom.h` | `18b01de1f4ae82696c7f31f519eb9294361f2e8b8adf871ce0d783d233a31ec9` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleRandom.cpp` | `75312b1d1fc7f07c25cac3945ef58cd1f3853e6515a2d01bc154c4ffcb3aa8a4` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleIdentifiersTests.cpp` | `5253740a0aedfc704752d0f130856e3e14db81b64cdb50d2e9c9175c58ec2619` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleSetupTypesTests.cpp` | `9320216c7c6a4c166ea68c4ee94df40cf95d6b4b7a0f3f923414e87cc75ec770` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleRandomTests.cpp` | `6885e67a53232142427b513fa6775e12b2711e5207cab976a535788fb552becc` |

Protected hashes were identical before and after Unreal:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleStats.h` | `92028991c761de61439c37d5e006121194f42c19b10021185b6157ec168518de` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleDamageCalculator.h` | `6b4951eba72e3782d392fdf16cfd7f4dc27227843def6a8af019e959d717fe38` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDamageCalculator.cpp` | `f9a61783d19d37dfc7f931d2eaf4f381a1fb52ab06360d3fe209a77326fdc7c5` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleDamageCalculatorTests.cpp` | `c8c32253ff4332ee745d79a897d34c4a23b7d6e43bf456923fc428bc8bd35db1` |
| `Game/Source/PokemonSolarus/PokemonSolarus.Build.cs` | `5055df3ec3790fb34ef6113f2f47ebe1bdafb0cda2e3ae3cd5b5e631a216d8b7` |
| `Game/PokemonSolarus.uproject` | `97d07ae09b7fbcb7e095ebfd5a4a15c1e1c2953e40e93ec2c89bf407257af7e5` |
| `Game/Config/DefaultEngine.ini` | `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920` |

The shared `Tests` and `Acceptance` sections above describe all of C01. C01B's
completion handoff below records the successful engine, decision, event,
snapshot, setup-shape, stale-decision, and canonical replay gate. C01 is now
complete.

## C01B Completion Handoff

Status date: 2026-08-20  
Successful run ID: `C01B-Contracts-20260820T131414Z`  
C01 status: Complete  
Next eligible packages: C02A and C02B; default sequential continuation is C02A
in a new session

### Owned files

C01B added only these runtime and focused-test files:

- `Game/Source/PokemonSolarus/Public/Battle/BattleSetup.h`
- `Game/Source/PokemonSolarus/Public/Battle/BattleDecision.h`
- `Game/Source/PokemonSolarus/Public/Battle/BattleEvent.h`
- `Game/Source/PokemonSolarus/Public/Battle/BattleSnapshot.h`
- `Game/Source/PokemonSolarus/Public/Battle/BattleReplay.h`
- `Game/Source/PokemonSolarus/Public/Battle/BattleEngine.h`
- `Game/Source/PokemonSolarus/Private/Battle/BattleSetup.cpp`
- `Game/Source/PokemonSolarus/Private/Battle/BattleDecision.cpp`
- `Game/Source/PokemonSolarus/Private/Battle/BattleEvent.cpp`
- `Game/Source/PokemonSolarus/Private/Battle/BattleReplay.cpp`
- `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleSetupContractTests.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleEngineContractTests.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleReplayContractTests.cpp`

This handoff and `00-roadmap-index.md` are the only status documents updated.
C01B did not edit C01A, the existing calculator, module rules, `.uproject`,
Unreal config, B00B snapshot, Solarus interview handoff, content/assets, or
unrelated sprite/importer work.

### Frozen C01B contracts

- `FBattleSetup` validates Single, Double, and PartnerDouble ownership/slot
  shapes atomically, then stores canonical immutable setup facts. Its settings
  and catalog dependencies are versioned references; C01B does not guess
  C02B's future catalog interface.
- `FBattleDecisionRequest`, `FBattleDecision`, and `FBattleRejection` expose
  typed request/payload/reason data, canonical legal-value lists, involved IDs,
  and echoed state versions without display text.
- `FBattleEngine` uses a private implementation and remains the sole mutable
  owner. Public snapshots are deep value copies with monotonic state versions.
  A private test fixture supplies the synthetic no-mechanics decision and
  opponent-removal checkpoint; production creation does not fabricate Fight or
  other later-package mechanics.
- Stale, malformed, and terminal decisions are rejected without state-version
  or RNG changes. The synthetic accepted action receives one stable action ID
  and produces a totally ordered trace. Between-actions stat refresh validates
  the state version, battler, phase, and one-use opponent-removal checkpoint.
- `FBattleEvent` freezes every required minimum event family and carries stable
  ordinals and battle/turn/action/resolution identity, typed cause/source/
  targets, numeric change fields, simultaneous-group and hit metadata, and
  visibility/reveal facts. `FBattleResolution` owns its immutable ordered event
  list.
- `FBattleReplayRecord` validates and owns canonical setup, decision, refresh,
  resolution, RNG, and final-snapshot inputs. Its serializer writes an explicit
  versioned big-endian byte stream with fixed field/order/enum encodings; it
  never writes raw struct bytes, padding, pointers, or unordered iteration.
- The new contracts contain no World, Actor, UObject, Blueprint, Data Table,
  presentation, loader, `FText`, global-random, or raw-memory serialization
  dependency.

### Validation evidence

- The first source-compiling Editor build,
  `C01B-EditorBuild-20260820T131146Z`, found 13 local C01B compiler errors and
  0 warnings. The fixes stayed inside C01B. Preserved log:
  `Game/Saved/Logs/C01B-EditorBuild-20260820T131146Z.log`, SHA-256
  `1fd69ab1e0bec80db81e08e1b3a890a03172251c170c810db49ce60d40ba587d`.
- The subsequent `PokemonSolarusEditor Win64 Development` build succeeded with
  0 compiler warning lines and 0 compiler error lines. Log:
  `Game/Saved/Logs/C01B-EditorBuild-20260820T131322Z.log`, SHA-256
  `ef607f7339799707df2026d4792ae7bdcca8291fb9a44f76f51509a46d4fa216`.
- `PokemonSolarus.Battle.C01B` performed exactly nine tests: 9 succeeded, 0
  with warnings, 0 failed, 0 not run, exit code 0. Report:
  `Game/Saved/Automation/C01B-Contracts-20260820T131414Z/index.json`, SHA-256
  `b0bd14817b78cfe2919eebc6791a622957fc5e297e0394035103d69982ea913b`.
  Log: `Game/Saved/Logs/C01B-Contracts-20260820T131414Z.log`, SHA-256
  `e780dcd25486f1584ffba6bdff12be2ee0906e30a7ef0613eed9b5f2ac580471`.
- The protected `PokemonSolarus.Battle.CoreContracts` filter performed exactly
  seven tests: 7 succeeded, 0 with warnings, 0 failed, 0 not run, exit code 0.
  Report:
  `Game/Saved/Automation/C01B-C01A-CoreContracts-20260820T131508Z/index.json`,
  SHA-256
  `2c035b9e038998a8ea7732118bbf03c1dee58b12a22ac331b288a57683563d17`.
  Log: `Game/Saved/Logs/C01B-C01A-CoreContracts-20260820T131508Z.log`,
  SHA-256
  `f51fbaf8c6b1d2e197ca2ea082815a897a218fc1acb4898dacdd566bd33074b4`.
- The protected `PokemonSolarus.Battle.DamageCalculator` filter performed
  exactly four tests: 4 succeeded, 0 with warnings, 0 failed, 0 not run, exit
  code 0. Report:
  `Game/Saved/Automation/C01B-DamageCalculator-20260820T131555Z/index.json`,
  SHA-256
  `940c9239a8d8dadfbadc40126c5d01a91c32c7580bc6ef9f8b7a3ead79df26a1`.
  Log: `Game/Saved/Logs/C01B-DamageCalculator-20260820T131555Z.log`, SHA-256
  `d20ae4623bde8ae0e86e10d07d18f9bd7e4be10fdd49fa39f268796cdfc6a1bd`.
- The full `PokemonSolarus.Battle` filter performed exactly 20 tests: 20
  succeeded, 0 with warnings, 0 failed, 0 not run, exit code 0. Report:
  `Game/Saved/Automation/C01B-FullBattle-20260820T131636Z/index.json`, SHA-256
  `038b928ad589657b2ea3fbb99a18b87e64b5a7359bbdeedaae3a4cef38cedba7`.
  Log: `Game/Saved/Logs/C01B-FullBattle-20260820T131636Z.log`, SHA-256
  `b10997b00ea5928cd2b19762dafbcecbfe933ab7e6ff4671b744607f106c893e`.
- All four JSON reports record 0 test warnings and 0 failures. Separate startup
  noise consists only of optional profiler/GPU-capture/tablet DLL messages and
  Unreal's own `UE::UnifiedErrorTest` diagnostics. The logs contain no
  Automation or Solarus battle issue line.
- Every hash in the preceding C01A and protected-file tables was rechecked and
  matched after Unreal. `Game/Config/DefaultEngine.ini` remained at SHA-256
  `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920`;
  no generated Android File Server change needed cleanup.

Final C01B source/test hashes:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleSetup.h` | `2c5525705fc805c103e0d3e1b89d4c0cfc2d221c72ea360eaee86a9db3799cfd` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleDecision.h` | `7e1306130f62468e05f6b8e20d2ac35dcb8b788f8c9bc01ce3753384c4723f0e` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEvent.h` | `b80fccbfe8185272cc655af41f1f4a187a0843ca50e68a08de28eb9291b0754a` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleSnapshot.h` | `8c99db5c57ba5e49f3aac988f6659ea72d64ba215b16fb12d88d2c4afbf13738` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleReplay.h` | `23c0bb101c68b56aff6ec60a20881f4d2518294772376822d2dd492bdc2220dc` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleEngine.h` | `7587a038be4ae43808c38215f882e19b1adff1a0d27b2494b01ffca275f22122` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleSetup.cpp` | `b87217cb965075f800d592cc3c0c36cf59a9463f03f19c54d220140c3ddd052c` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDecision.cpp` | `9f4fbc5aa5fd53f6670dd292ecb62e8dd611ff382ed2d921b9e6741450f5436a` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEvent.cpp` | `960d346df2c504b54ae06b53697e8d659ca8f4c7994a51e4664a77e1cbe73e5f` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleReplay.cpp` | `c42833fb459c5dcc3828550851c3fd8d3002795bfc7517283d5bcb8df8012fd1` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp` | `e7a2669a2f61a73a7f444713e8ea39467d21012563c027f29c37e87aa4df29e5` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleSetupContractTests.cpp` | `7b6c7536f4448dfd90606cdf62d2345b2d6b9be05a18d7a21e99f30c6959bb1c` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleEngineContractTests.cpp` | `23482323ab5e5d33c1497393432c15d38680beccc953f3111ae3486245547fda` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleReplayContractTests.cpp` | `6e5eca79c9bc07042dc57b8fb8fce1a48773f38d270c648c332bfd956c87e498` |

## C10A remediation core-contract addendum — 2026-08-30

R1 through R6 reused C01's stable identity, event, deterministic RNG, and replay
contracts. Their only public-event vocabulary additions were append-only:

| Lane | C01-owned effect |
|---|---|
| R1 (`06d884e`) | Extended target-resolution event validation for `SelectedOtherBattler` and `FixedOpponentSpreadSet`; no existing target value or event type changed. |
| R2 (`cf8b3e6`) | Appended `TargetRedirectionRegistered = 53` and retained exact action/occupant identity, event order, and no-target-RNG behavior. |
| R3 (`1294c7c`) | Appended `ActionPowerModifierRegistered = 54` with validated source, ally, action identity, and public registration counts; the private C03/C05 registration retains the exact rational magnitude. |
| R4A/R4B (`0f8664b`/`ef93d5d`) | Reused existing hit, effect, RNG, and publication events; authored always-hit branches consume no accuracy draw and ordinary/chance branches retain their existing draws. |
| R5 (`7686395`) | Appended `ItemRestored = 55` and `ItemTransferred = 56`, and tightened the existing `ItemRemoved` mutation-event shape without changing its ordinal. |
| R6 (`74b1c1b`) | Reused existing condition events and chance RNG; an authored optional absent removal emits no condition mutation event, while a present removal follows the normal event order. |

Replay schema 6 remains the current schema. These lanes added no global RNG,
raw-memory serialization, display text, or second publication path. Independent
R7 later accepted the combined source and the 390/390 full-Battle report.
