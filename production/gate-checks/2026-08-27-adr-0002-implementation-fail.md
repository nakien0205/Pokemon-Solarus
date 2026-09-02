# ADR-0002 Implementation Gate — 2026-08-27

> **Historical gate:** This FAIL verdict applies to HEAD
> `f48146f4f439930ed06f5f7feaf957514bcc4408` and the 594-success evidence root
> recorded below. It was superseded by
> `production/gate-checks/2026-08-27-adr-0002-implementation-pass.md` at HEAD
> `b5db3e440d7c6eb5ba6ddbcc01a92a3c9b8756c0`, using final evidence root
> `Game/Saved/AutomationReports/ADR0002-StaleBag-Final-20260827-164919`.
> The historical path and totals below are intentionally unchanged.

## Verdict

**FAIL**

ADR-0002 is Accepted as a design, and its broad validation baseline is green,
but the live implementation still has one atomicity blocker. Do not start C10A
until that blocker is repaired, failure-proven, and revalidated.

Chain-of-Verification: 5 questions checked — verdict unchanged.

## Scope

- Gate target: ADR-0002 implementation completion before C10A.
- Current HEAD: `f48146f4f439930ed06f5f7feaf957514bcc4408`.
- Roadmap state: B00 through C09 package delivery complete; C10 and C11 remain.
- Next package after this gate passes: C10A Required Canonical Rows.
- This gate did not authorize or change production source, tests, assets,
  configuration, generated Unreal output, or Git state.

## Required artifacts and current evidence

- [x] Accepted ADR:
  `docs/architecture/adr-0002-battle-encounter-runtime-authority-and-atomic-resolution-commit.md`.
- [x] Live implementation and focused ADR tests exist.
- [x] Final exported evidence exists at
  `Game/Saved/AutomationReports/ADR0002-Task5-Final-20260827-111636`.
- [x] All 22 exported `index.json` files are readable.
- [x] The 22 reports total 594 successes, 0 succeeded with warnings, 0 failed,
  0 not run, and 0 in process.
- [ ] Every accepted lifecycle path obeys prepare/stage/commit and is
  failure-proven. The stale accepted Bag-cancellation path does not.

Evidence totals:

- ADR-0002 filter: 104 successes.
- Remaining affected package/runtime filters: 176 successes.
- All 21 pre-full-suite reports: 280 successes.
- Full `PokemonSolarus.Battle` filter: 314 successes.
- Total across all reports: 594 successes.

The green reports are valid baseline evidence. They are not proof for a branch
that the tests never force to fail.

## Hard blocker

### B1 — Accepted stale Bag cancellation mutates live state before all failure points are prepared

File:
`Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp:14282-14324`

`FBattleEngine::ExecuteCurrentBagItem` defines `FinishAcceptedAction`, which:

1. sets `Action->bFinished = true`;
2. appends `ActionCompleted`;
3. advances `CurrentLockedActionIndex`;
4. calls the live-mutating `AppendPostActionBoundaryEvents` when the Battle is
   not terminal;
5. increments `StateVersion`;
6. only then validates invariants and constructs the resolution; and
7. uses `check` for failures instead of returning a prepared rejection while
   preserving the pre-call state.

`AppendPostActionBoundaryEvents` at
`Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp:4054-4101` mutates
queue-boundary and replacement-request state directly. Replacement-request
construction can fail, but the helper treats that with `check` after live state
has already changed.

`CancelStaleUse` at
`Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp:14385-14394` routes
accepted stale Bag uses through that helper. Its call sites cover stale target
or Bag-policy facts, newly illegal or mismatched item use, and invalid or
capacity-blocked Capture input.

This violates ADR-0002's rule: validate, prepare, resolve/stage, then commit
once. A recoverable failure must not partially change the action, cursor,
boundary state, state version, events, or resolution history.

## Missing proof

The existing stale Capture test at
`Game/Source/PokemonSolarus/Private/Tests/BattleAtomicCheckpointTests.cpp:4401-4458`
proves the nominal accepted-cancellation result only. The stale Hyper Potion and
X Attack cases in
`Game/Source/PokemonSolarus/Private/Tests/BattleBagItemTests.cpp` likewise prove
no item, quota, or RNG consumption on nominal cancellation.

They do not inject failure during post-action boundary planning, replacement
request construction, invariant validation, or resolution construction. They
therefore cannot prove rollback-free atomicity for B1.

## Maximum path to PASS

### 1. Repair the checkpoint as one staged transaction

Touch:

- `Game/Source/PokemonSolarus/Private/Battle/BattleEngine.cpp`

Required result:

- Capture a checkpoint identity before preparation.
- Build `ActionCanceled`, `ActionCompleted`, terminal/boundary events,
  replacement requirements, pending decision requests, the next cursor/state
  version, and the candidate resolution without changing live state.
- Use the existing const/staged queue-boundary resolver rather than the legacy
  live-mutating void path.
- Recheck identity immediately before commit.
- Apply the complete state delta and append exactly one resolution in one final
  commit.
- Convert every recoverable preparation failure into the established typed
  rejection path. Do not use `check`, `checkf`, `ensure`, Fatal logging, or
  partial live mutation as the failure contract.
- Keep terminal and mandatory-replacement event order unchanged.

### 2. Add complete failure and route proof

Touch:

- `Game/Source/PokemonSolarus/Private/Tests/BattleBagItemTests.cpp`
- `Game/Source/PokemonSolarus/Private/Tests/BattleAtomicCheckpointTests.cpp`

Required proof:

- Cover non-Capture stale Bag cancellation and stale Capture cancellation.
- Cover every `CancelStaleUse` call-site family, including a path that reaches
  mandatory replacement and must build replacement requests.
- Inject each recoverable failure after checkpoint identity capture and before
  commit.
- For every rejected failure, compare before/after observations for:
  - item counts and Bag ownership;
  - Bag-action quota;
  - gameplay RNG state and trace;
  - current action identity and `bFinished` state;
  - locked-action cursor;
  - phase, pending replacements, and pending decisions/requests;
  - battler, active-slot, pending-Capture, and outcome state;
  - state version;
  - raw event history and event ordinals;
  - resolution history and next resolution identity; and
  - replay-visible facts.
- Prove the successful accepted-cancellation path still publishes exactly one
  resolution with exact event order and advances exactly once.
- Prove stale checkpoint identity rejects without mutation.

### 3. Build and run the full approved evidence matrix

Use the established forced-Unity editor build settings:

- `-ForceUnity`
- `-DisableAdaptiveUnity`
- `-BytesPerUnityCPP=1`
- `-NoUBA`

Export and inspect these 22 reports in order:

1. `PokemonSolarus.Battle.ADR0002`
2. `PokemonSolarus.Battle.C03A`
3. `PokemonSolarus.Battle.C03B`
4. `PokemonSolarus.Battle.C04A`
5. `PokemonSolarus.Battle.C04B`
6. `PokemonSolarus.Battle.C05A`
7. `PokemonSolarus.Battle.C05B`
8. `PokemonSolarus.Battle.C05C`
9. `PokemonSolarus.Battle.C06A`
10. `PokemonSolarus.Battle.C06B`
11. `PokemonSolarus.Battle.C07A`
12. `PokemonSolarus.Battle.C07B`
13. `PokemonSolarus.Battle.C07C`
14. `PokemonSolarus.Battle.C07D`
15. `PokemonSolarus.Battle.C08A`
16. `PokemonSolarus.Battle.C08B`
17. `PokemonSolarus.Battle.C08C`
18. `PokemonSolarus.Battle.C09A`
19. `PokemonSolarus.Battle.C09B`
20. `PokemonSolarus.Battle.C09C`
21. `PokemonSolarus.Battle.Runtime`
22. `PokemonSolarus.Battle`

For every exported `index.json`, require:

- the intended filter discovered tests;
- every reported test path belongs to the intended filter;
- `succeededWithWarnings = 0`;
- `failed = 0`;
- `notRun = 0`;
- `inProcess = 0`; and
- every test entry has zero warnings and errors.

Do not judge success from process exit code alone. Rerun this gate against the
post-fix checkout and the new report root.

### 4. Continue the roadmap only after PASS

The approved order is:

1. ADR-0002 stale Bag cancellation remediation.
2. Fresh ADR/affected/full-Battle evidence.
3. Fresh implementation gate PASS.
4. C10A Required Canonical Rows.
5. C10B Canonical Proofs.
6. C11A Full Integration.
7. C11B Release Gate.

## Files not to touch for this remediation

Do not touch unless new live evidence proves a direct dependency and the user
approves a revised write set:

- `docs/registry/architecture.yaml`;
- absent follow-up architecture records;
- `docs/battle-engine-structural-split-handoff.md`;
- other Battle production or test files;
- replay schema, enum ordinals, encounter policies, or selector contracts;
- Cry for Help, reinforcement, or `CallReinforcement` code and tests;
- C10 JSON, Data Tables, canonical content, or proof assets;
- UI, HUD, Blueprint, map, art, material, texture, animation, or audio assets;
- `.uproject`, module rules, plugins, or configuration;
- `Game/Binaries`, `Game/Intermediate`, or hand-edited `Game/Saved` output;
- Git index, commits, branches, remotes, or history; and
- the later structural split.

The existing staged `FBattleFaintOutcomeResolver::ResolveQueueBoundary`
overloads already provide the needed direction. Do not rewrite the resolver or
broaden the architecture unless the bounded implementation proves that seam is
insufficient.

## Lifecycle audit summary

| Lifecycle family | Focused proof | Gate state |
|---|---:|---|
| Setup policies 3B1/3B2 | 4 + 5 | Ready |
| Transactional RNG 3C | 5 | Ready |
| Run/WildFlee 3D1 | 7 | Ready |
| Non-Capture Bag 3D2 | 2 plus C08C | Concern: stale cancellation shares B1 |
| Capture 3D3 | 6 | Calculation/resource/RNG staging ready; stale cancellation shares B1 |
| Action start 3E1 | 8 | Ready |
| Voluntary Switch 3E2 | 8 | Ready |
| Pivot continuation 3E3 | 10 | Ready |
| Pre-move gates and PP 3E4 | 19 | Ready |
| Target resolution 3E5 | 12 | Ready |
| Move effects/faint/outcome 3E6 | 18 | Ready |

No second hard implementation blocker was found in the audited lifecycle
families. This is not a claim about unreviewed systems outside ADR-0002.

## Producer concerns and documentation attention

Producer verdict during the gate: **CONCERNS**.

The primary document needing immediate attention was
`production/session-state/active.md`, because repository instructions require
every continuation session to read it. It still directed work straight to C10A
and did not record the final ADR-0002 gate or blocker.

Other stale authorities were:

- `plan/battle_mechanics/00-roadmap-index.md`, which called C10 dependency-clear;
- `plan/battle_mechanics/11-canonical-proof-content.md`, which still said C10
  was blocked by already-complete C07D/C08/C09 work;
- `plan/battle_mechanics/12-integration-and-release-gate.md`, whose C11 status
  was too broad to show the real remaining order;
- the top status lines in B00, C04, and C06 package documents;
- a historical follow-up architecture draft, which still named an already-fixed Capture ordering defect; and
- the structural-split handoff, which still called target and effect checkpoint
  work outstanding.

The approved documentation cleanup corrected those statements. The Producer
concern that remains is operational: do not let the corrected roadmap wording
be interpreted as permission to enter C10A before the implementation gate
passes.

## Director Panel Assessment

Creative Director: **READY**

- The repair preserves the accepted Battle rules and the C10/C11 content order.
  No new design decision is required.

Technical Director: **CONCERNS**

- The stale accepted Bag-cancellation helper violates the atomic checkpoint
  contract despite the green broad report set.

Producer: **CONCERNS**

- Active status and package sequencing were stale. They required correction,
  and C10A must remain held behind a fresh implementation PASS.

Art Director: **READY**

- The remediation needs no visual or asset changes. User-owned presentation
  work remains excluded.

The Technical concern is a contract failure, so the automatic overall verdict
is **FAIL**.

## Chain-of-Verification answers

1. **Did the gate separate hard blockers from recommendations?** Yes. B1 is the
   only hard blocker. The structural split and broader cleanup are explicitly
   excluded recommendations.
2. **[TOOL ACTION] Were green reports treated too leniently?** No. All 22
   exported `index.json` files were reread: 594 successes and zero bad counters.
   The live tests were also reread and do not inject the B1 failure branch.
3. **[TOOL ACTION] Is there another live-first lifecycle family?** The named
   ADR-0002 lifecycle implementations and focused tests were re-searched. The
   other audited families use staged deltas/contexts and have focused proof; no
   second hard blocker was found.
4. **Does the maximum path include enough work to prevent another gate
   failure?** Yes. It covers all stale-cancellation routes, fallible boundary and
   resolution preparation, full before/after invariants, exact publication, the
   affected filters, and the full Battle suite.
5. **Does B1 require a new design decision?** No. ADR-0002 already states the
   correct rule. This is a bounded implementation and proof defect.
