# B00 — Live Baseline and Rules Snapshot

Priority: P0  
Status: B00A and B00B complete; B00 accepted; downstream B00-C09 package delivery complete

Hard dependencies: None  
Required order: B00A, then B00B

## Objective

Replace stale implementation claims with reproducible evidence for the live
numeric-only source, then freeze the exact modern rules needed by every later
package. B00 changes no battle mechanics.

## B00A — Live Baseline Verification

Use one session for evidence only.

### Required inspection

- Enumerate every current battle source and Automation test under
  `Game/Source/PokemonSolarus`.
- Confirm that `BattleStats.h`, `BattleDamageCalculator.h/.cpp`, and
  `BattleDamageCalculatorTests.cpp` are the only live battle implementation.
- Record SHA-256 hashes for those files, `Game/PokemonSolarus.uproject`, and
  `Game/Config/DefaultEngine.ini`.
- Record the project and engine versions. The expected engine is UE 5.8.1,
  changelist `56057345`.
- Record the absence or presence of Git at the time of execution.
- Treat saved 33-test reports and generated metadata for absent source as
  historical only.

### Required verification

1. Build `PokemonSolarusEditor Win64 Development` from the live source.
2. Run `PokemonSolarus.Battle.DamageCalculator` headlessly.
3. Export a new timestamped report and log.
4. Prove that the four live test paths are discovered and pass.
5. Confirm that no stale object supplies an absent coordinator or resolver.
6. Compare configuration and project hashes after Unreal.

Do not clean `Binaries`, `Intermediate`, `Saved`, or historical reports merely
to make the evidence easier to read. Any destructive cleanup needs separate
approval.

### Baseline acceptance

- The Editor target builds from the enumerated source.
- All four live calculator tests pass with zero failed, warning, or not-run
  tests in the exported report.
- The report includes source hashes and cannot be confused with Story 001's
  historical evidence.
- Unexpected file/config mutation is either reverted with approval or reported
  as a blocker.
- No source, GDD, story, or configuration file is changed by B00A. The package
  and roadmap evidence may be updated only when the user explicitly requests
  it.

### B00A execution evidence — 2026-08-20

Run ID: `B00A-DamageCalculator-20260820T090904Z`  
Result: Complete  
B00B status at the end of this B00A run: Not started; current B00B evidence is
recorded below

Live baseline:

- The module contains only its three boilerplate files plus
  `BattleStats.h`, `BattleDamageCalculator.h/.cpp`, and
  `BattleDamageCalculatorTests.cpp`.
- Those four battle files are the only live battle implementation and test
  source. They expose numeric stats, deterministic Physical/Special base
  damage, and four calculator Automation tests only.
- UE 5.8.1 was verified from the installed `Build.version`: changelist
  `56057345`, compatible changelist `55116800`, branch
  `++UE5+Release-5.8`.
- Git was present at the execution baseline on clean `main`, initial commit
  `d302018d4cd7d11a40b55c2003e164345b5011f7`. During the initial read-only
  inspection it changed externally from an unborn dirty branch to that commit;
  B00A ran no Git write or commit command.
- The saved 33-test Story 001 report and generated files for now-absent source
  remain historical evidence only.

Protected SHA-256 hashes, identical before and after Unreal:

| File | SHA-256 |
|---|---|
| `Game/Source/PokemonSolarus/Public/Battle/BattleStats.h` | `92028991c761de61439c37d5e006121194f42c19b10021185b6157ec168518de` |
| `Game/Source/PokemonSolarus/Public/Battle/BattleDamageCalculator.h` | `6b4951eba72e3782d392fdf16cfd7f4dc27227843def6a8af019e959d717fe38` |
| `Game/Source/PokemonSolarus/Private/Battle/BattleDamageCalculator.cpp` | `f9a61783d19d37dfc7f931d2eaf4f381a1fb52ab06360d3fe209a77326fdc7c5` |
| `Game/Source/PokemonSolarus/Private/Tests/BattleDamageCalculatorTests.cpp` | `c8c32253ff4332ee745d79a897d34c4a23b7d6e43bf456923fc428bc8bd35db1` |
| `Game/PokemonSolarus.uproject` | `97d07ae09b7fbcb7e095ebfd5a4a15c1e1c2953e40e93ec2c89bf407257af7e5` |
| `Game/Config/DefaultEngine.ini` | `cc089de5c5094ab055af54e81f4b518e799afa9adaebf542e0d45bea46ba2920` |

Verification:

- `PokemonSolarusEditor Win64 Development` returned `Result: Succeeded`.
  Unreal Build Tool regenerated and evaluated the live action graph with
  `-NoUBTMakefiles`; the target was already up to date, so it executed zero
  compile actions.
- `PokemonSolarus.Battle.DamageCalculator` discovered exactly four tests. The
  exported report records 4 succeeded, 0 succeeded with warnings, 0 failed,
  and 0 not run; the command exited 0.
- Thirteen stale coordinator/resolver/presenter object files were deliberately
  retained under `Intermediate`. None appears in the current module DLL linker
  response, which names only the module boilerplate, calculator, calculator
  tests, generated inline code, and resources. Runtime discovery likewise
  exposed only the four live calculator tests.
- `PokemonSolarus.uproject` and `DefaultEngine.ini` remained byte-for-byte
  unchanged. The pre-existing Android File Server block was preserved exactly.
- Separate startup noise consisted of one user-layout compatibility warning
  and missing optional profiler/tablet DLL notices. The test report itself has
  zero warnings and errors; the log has no `Error:`, fatal, or assertion line.

Fresh evidence:

| Artifact | SHA-256 |
|---|---|
| `Game/Saved/Automation/B00A-DamageCalculator-20260820T090904Z/b00a-evidence.json` | Self-describing manifest; not self-hashed |
| `Game/Saved/Automation/B00A-DamageCalculator-20260820T090904Z/index.json` | `022985624b443952edda1682c2ae071c12950a56fd6edcb1307fbbd61440fe54` |
| `Game/Saved/Logs/B00A-DamageCalculator-20260820T090904Z.log` | `66ca06e8886c09525e2801c6fa6f7dea902c9f9c57c290324570e49f21cb995d` |
| `Game/Saved/Logs/B00A-EditorBuild-20260820T090904Z.log` | `76a3d3637994f14ecf2a73d7c37dac66d199e34a7d5b47d1aa7efadf81311ea0` |

This execution stops here. It did not research, draft, or modify B00B.

## B00B — Modern Rules Snapshot

Use a separate documentation/research session after B00A. The output is
`plan/battle_mechanics/reference/modern-rules-snapshot.md`.

### Source precedence

1. Explicit Solarus decisions in the completed battle handoff.
2. Pinned Pokemon Showdown commit
   `34caa98811fd6ed5d2f173ec1fc29dd9bd4bc91d`.
3. Pinned Smogon damage-calc commit
   `83807801012f0af3e2dbb543d6fd40b483b3ebab` for damage calculations.
4. Pinned Scarlet capture disassembly revision
   `31d6aa136883ab354f5e8151526ee40f07317be0` for capture behavior.
5. If the evidence is missing or conflicts, stop the affected rule and ask the
   user. Do not invent a compromise.

### Required snapshot sections

- Permanent stat formulas, validation ranges, nature multipliers, and every
  integer rounding point.
- Stat-stage and accuracy/evasion tables for `-6` through `+6`.
- The complete 18x18 type chart and dual-type multiplication order.
- Move accuracy, always-hit rules, PP, priority, Struggle, critical-hit stages,
  random damage values, STAB, burn, screens, spread modifiers, and exact final
  damage order.
- RNG eligibility and call order for every probabilistic check in scope.
- Action priority, effective-Speed ordering, Solarus cross-side and same-side
  tie rules, and Quick Claw interaction.
- Standard obedience: required setup facts, eligibility, exact action checkpoint,
  RNG calls, every possible disobedience result, resource consumption, and
  ordered events.
- Target redirection, semi-invulnerability, Protect, spread targets, captured
  target cancellation, forced/pivot switching, and faint checkpoints.
- Major-status application, immunity, action gates, durations/counters, damage,
  cure, switch behavior, and faint cleanup.
- The approved volatile conditions, including failure and expiry rules.
- Weather, terrain, hazards, screens, rooms, Tailwind, Safeguard, and Mist:
  duration, layers, effects, entry processing, removal, and interaction order.
- Every approved Ability and held item: trigger, public reveal, suppression,
  ordering, duration, and cleanup.
- Hyper Potion, Revive, Full Heal, X Attack, and Poke Ball behavior.
- Full Heal's exact major-status and volatile cure set.
- Configured wild-opponent fleeing: explicit enablement, authored eligibility/
  probability/trigger inputs, self-removal, queued-action cancellation, and the
  Escape-with-opponent-fled cause when no wild opponent remains.
- Scarlet/Violet capture math, critical capture, shake RNG, status/HP/catch-rate
  factors, and external snapshot inputs.
- End-of-action, faint, replacement, end-of-turn, and terminal-outcome order.

### Solarus exceptions that must be copied exactly

- Cross-side exact ties favor the player side; same-side ties use RNG.
- Simultaneous final fainting resolves as player Defeat.
- A captured target cancels rather than redirects queued targeted moves, and
  those canceled moves consume no PP.
- Cry for Help and wild reinforcement are **Freeze until call by user**.
  Existing related code is left unchanged; the mechanic is not an active gate.
- Escape uses the exact Solarus C09 Run formula, not a canonical game formula.
- Modern friendship combat bonuses are disabled.
- No transformation gimmicks or overworld status damage.

### B00B acceptance

- Every later package can cite an exact rule, table, algorithm, or stop
  condition without researching independently.
- Every formula shows its integer operations and rounding boundaries.
- Every random rule says when a roll is consumed and when it is not.
- Every trigger family has a deterministic ordering key.
- Open questions identify the blocked package and are presented to the user.
- C01 remains blocked until the snapshot has no unresolved P0 rule.

### B00B completion evidence — 2026-08-20

Result: Complete and accepted  
Accepted output:
`plan/battle_mechanics/reference/modern-rules-snapshot.md`  
Accepted output SHA-256:
`ded20d707ab67c2bb4d883df8ac07cbd20e38a31766b8a9ef14515c93168ad50`

Verified reference locks:

| Reference | Exact revision/version |
|---|---|
| Pokemon Showdown | `34caa98811fd6ed5d2f173ec1fc29dd9bd4bc91d` |
| Smogon damage-calc | `83807801012f0af3e2dbb543d6fd40b483b3ebab` |
| Scarlet 1.1.0 capture parent | `31d6aa136883ab354f5e8151526ee40f07317be0` |
| Solarus handoff | SHA-256 `476040bcaf0cfdb9d7f97d3fba3ddc752f7760de252bcc7a8775f846a58c98a6` |
| Installed UE 5.8.1 `DataTable.h` | SHA-256 `1644b15509198b370716c3a106193f3bc642f40ab646235526c69fb8db4a73a4` |
| Bulbapedia Obedience | `oldid=4554429`; raw SHA-256 `b53f050d074c5c9e58d9364754f4ea1a2cd0c63449a8db0f8e70ab2576d2d97f` |
| Bulbapedia Priority | `oldid=4535978`; raw SHA-256 `2502da8f8cadeca76ff9b2d3fbe916cb83b77629279251751928ca32624214b4` |
| Bulbapedia Revive | `oldid=4594350`; raw SHA-256 `e650ef2f9d71ebf9be981d5f21485b61e11c90ba7197cc20ca774b5ab0c1d81a` |
| Bulbapedia Full Heal | `oldid=4614360`; raw SHA-256 `eec2a5f9863c102e070df707c942949a0e3e7de0c51bdf7135fccc57b0875549` |
| Bulbapedia X Attack | `oldid=4595515`; raw SHA-256 `056ec4a271f7b73f81e13525877e2d167856bccf63689e0ab6605fa97eebf31d` |
| SacredPhoenix obedience report | fetched PDF SHA-256 `1d53fcb0e807cc386abeb633d6d5556573b87dbe06831082ab2d22673356e7f1`; diagnostic only |
| Smogon disobedience discussion | fetched HTML SHA-256 `a6f868f74c20043b1538a104d482d8570683b8cec01b3bc101be00e7c11c3a31`; diagnostic only |

The snapshot records SHA-256 hashes for every extracted Showdown/damage-calc
file used, all ten capture child revisions and content hashes, the full 18x18
chart, formulas, rounding, eligibility and RNG consumption, trigger keys,
Solarus exceptions, the supplementary source limits, and the UE Data Table copy
boundary.

Resolution of `Q-B00B-01`:

- The user authorized the narrow supplementary Gen IX reference set. Every
  supplementary artifact used by the accepted snapshot is pinned and hashed in
  the table above and in the snapshot.
- The evidence established obedience reference levels and badge caps, the
  shared pre-move placement of special commands, Revive's half-HP effect, Full
  Heal's cure set, and X Attack's two-stage effect.
- The review stopped before editing when it did not establish the exact modern
  obedience algorithm, relative special-command order, odd-HP Revive rounding,
  or X Attack behavior at `+6`.
- The user then approved explicit Solarus closures: deterministic above-cap
  refusal before PP/RNG; `Run > voluntary switch > Bag/capture > moves`;
  `max(1, floor(MaxHP / 2))` Revive HP; and rejection of X Attack at `+6`
  without item/action consumption.
- Full Heal cures the frozen major-status set and Confusion only. Bag/capture
  and Run revalidate at execution; resources and RNG begin only after legality.
- `Q-B00B-01` is resolved and blocks no package. No unresolved P0 rule remains
  in B00B.

Tracking/configuration evidence:

- With explicit user authorization, `.gitignore` no longer hides `docs/`,
  `design/`, `src/`, `plan/`, or `production/`. This makes the durable design,
  plan, and evidence files visible to Git.
- Generated Unreal directories `Binaries/`, `Intermediate/`, and `Saved/`,
  generic logs, and `production/session-logs/` remain ignored.
- `.gitignore` SHA-256 after the narrow change is
  `0f1da0b6bf5ba95c2514d19b036f0e2ee7a8d899c2530bf898a9e5d8f13f24e9`.
- B00B did not invoke Unreal and did not modify battle source,
  `PokemonSolarus.uproject`, or `DefaultEngine.ini`. The B00A configuration
  preservation evidence remains authoritative.
- C01 was not started.

Exact continuation: B00 is complete and accepted. Stop before C01A in this
session. The next session begins C01A by reading the roadmap, C01 package, live
numeric-only source, and accepted snapshot at the hash above.

## Handoff

At completion, update `00-roadmap-index.md` with:

- B00A/B status.
- Evidence paths and source hashes.
- Reference commits used.
- Any packages blocked by unresolved evidence.
- The exact next session: C01A.

## Package Acceptance

Package result: Accepted on 2026-08-20.

- B00A has a fresh report tied to the live source hashes and no source/config
  mutation.
- B00B freezes every P0 formula, ordering rule, and RNG-consumption rule needed
  by C01-C03.
- Any unresolved later rule names its blocked package and is not silently
  guessed.
- The index links the evidence, the completed snapshot, and C01A as the next
  session.
