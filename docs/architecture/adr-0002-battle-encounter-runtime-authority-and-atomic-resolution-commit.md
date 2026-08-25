# ADR-0002: Battle Encounter Runtime Authority and Atomic Resolution Commit

## Status

Accepted

## Date

2026-08-25

## Engine Compatibility

| Field | Value |
|-------|-------|
| **Engine** | Unreal Engine 5.8.1 |
| **Domain** | Core |
| **Knowledge Risk** | HIGH — the pinned engine version post-dates the configured workflow knowledge baseline |
| **References Consulted** | `docs/engine-reference/unreal/VERSION.md`, `docs/engine-reference/unreal/breaking-changes.md`, `docs/engine-reference/unreal/deprecated-apis.md`, Epic documentation for assertions, smart pointers, arrays, and array views |
| **Post-Cutoff APIs Used** | None. This decision uses plain C++ Battle contracts and established Unreal value/container ownership patterns. |
| **Verification Required** | Focused C09 validation, every affected older package filter, and the full `PokemonSolarus.Battle` suite. Acceptance is based on exported Automation `index.json` counters, not process exit code alone. |

## ADR Dependencies

| Field | Value |
|-------|-------|
| **Depends On** | ADR-0001 (Accepted) |
| **Enables** | C09 runtime-blocker remediation and current cross-package acceptance validation |
| **Blocks** | The reviewed C09 remediation implementation until this ADR is accepted |
| **Ordering Note** | Write this ADR as Proposed, run `/architecture-review` in a fresh independent session, accept or revise the decision, then implement runtime and test remediation in later bounded sessions. C10 sequencing remains a separate roadmap/user decision. |

## Context

### Problem Statement

C09 introduced compiled encounter policies, Capture, Run, configured WildFlee,
and Partner Double behavior, but the implementation does not yet establish one
complete runtime authority and commit boundary.

`FBattleSetup::TryCreate` successfully compiles
`FBattleCompiledEncounterPolicies` and then discards the result.
`FBattleEngineState::TryCreate` instead copies the raw authored
`FBattleEncounterPolicies`, and runtime helpers continue deriving legality from
raw encounter kind and policy booleans. The validated compiled representation
therefore is not the runtime authority it was designed to be.

Setup validation also admits shapes that contradict the accepted C09 rules:

- An ordinary living Wild opponent reserve can later enter through mandatory
  replacement even though only voluntary Wild switching is blocked.
- A Partner Trainer can be assigned controller modes other than the accepted
  resolved `Human` or `PartnerAI` modes.
- Capture badge count is incorrectly constrained like obedience badge count,
  although the capture rules define values above eight as the neutral badge
  modifier.

Capture execution has a separate transaction-boundary defect. The engine
applies the Bag contract to live Trainer state, consuming the Ball and Bag
quota, before calling the still-fallible capture calculation. A small positive
capture coefficient can pass input validation, later round the capture
indicator to zero, and cause `TryResolve` to fail after those resources were
already mutated. The current path then uses fatal logging.

Finally, replay schema is currently 6 while seven live tests still assert
schema 5, and an older Partner Team Victory test asserts behavior superseded by
C09C recovery. Historical focused C09 reports therefore do not prove current
cross-package acceptance.

### Constraints

- ADR-0001 remains authoritative for Battle-core state ownership, runtime
  composition, observer-safe presentation, and HUD behavior.
- Battle core remains the sole writer of transient Battle state.
- Authored setup remains immutable replay/configuration input.
- Core may emit typed persistence and progression facts but performs no
  external party, storage, reward, or save write.
- Preserve deterministic action, event, RNG, snapshot, and replay ordering.
- Preserve all existing enum ordinals unless a separately reviewed contract
  change requires an append.
- Cry for Help, wild reinforcement, and `CallReinforcement` remain
  **Freeze until call by user**.
- Do not introduce UI, assets, persistence, rewards, strategic AI, networking,
  GAS, or species-specific runtime branches.
- “Atomic” means one externally observable all-or-none transition for one
  locked-action execution checkpoint. A logical locked action may span multiple
  ordered checkpoints and resolutions. It does not mean lock-free or
  multithreaded execution.
- All reads and writes to one `FBattleEngine` instance are caller-serialized.
  Production callers use Unreal's game thread. Tests may use another single
  thread only for pure Battle-core/value logic; they must not access `UObject`,
  `AActor`, `UWorld`, UI, or other game-thread-only systems. Concurrent access
  is unsupported, and `FBattleEngine` provides no internal locking.

### Requirements

- One validated compiled policy object must govern runtime encounter legality,
  ownership, routing, and permissions.
- Unsupported encounter, role, controller, party, active-slot, and reserve
  shapes must fail before an engine state becomes observable.
- Every locked-action lifecycle checkpoint invocation must return and append
  the same `FBattleResolution` exactly once.
- Each accepted checkpoint commits only its owned resource, RNG, state, event,
  action-progress, state-version, and replay deltas all-or-none.
- A logical locked action may intentionally span multiple ordered checkpoints
  and resolutions. Facts committed by an earlier checkpoint remain committed
  unless a rule explicitly defines compensation. Action completion and cursor
  advancement occur only in the checkpoint that owns completion.
- The explicit four-phase mechanism is required when a checkpoint could mutate
  a resource, the parent RNG, or gameplay state before all fallible work owned
  by that checkpoint is complete.
  Existing checkpoints that complete all fallible work first and then apply one
  non-fallible transition do not require a mechanical refactor.
- When the explicit four-phase mechanism is required, validation and
  deterministic preparation consume no live RNG and mutate no Battle state.
- An aborted, rejected, stale, or internally unresolved checkpoint must not
  partially consume Bag items, Bag quota, PP, RNG, target state, events, action
  state, or replay-visible facts.
- A checkpoint rejection or cancellation may publish one complete typed
  resolution transition, including its defined queue-control, event, and replay
  facts, but no partial gameplay mutation or success facts.
- A legal gameplay failure, such as a valid capture attempt whose shakes fail,
  is a successfully resolved checkpoint and commits exactly the resources and
  RNG required by its rule.
- Recoverable authored/runtime failures return typed failure information.
  `check`, `ensure`, and fatal logging are not recovery mechanisms.
- Replay schema changes must update every dependent exact-version assertion and
  trigger affected-filter plus full-Battle validation.

## Decision

### 1. Make compiled encounter policy the runtime authority

Authored `FBattleSetup` fields remain the canonical configuration and replay
input. One successful `FBattleEncounterPolicyCompiler::TryCompile` result per
engine instance becomes the runtime authority.

The accepted `FBattleCompiledEncounterPolicies` value is stored by
`FBattleEngineState` and exposed internally through const access. Runtime
legality, request generation, selector routing, switching, Bag use, Capture,
Run, WildFlee, partner ownership, Revive permission, and scripted-ending checks
must consult that compiled authority or its trainer-specific policy.

Runtime code must not independently re-derive those permissions from raw
`FBattleSetup` policy booleans or encounter-kind branches. Public snapshots or
requests that expose policy-derived facts project them from the same compiled
authority.

Setup acceptance and engine creation must share or transfer the successful
compile result. Compiling solely to validate and then discarding the result is
not permitted. Replay reconstruction may compile again from the serialized
immutable setup because it creates a new engine instance.

### 2. Reject unsupported encounter shapes before engine creation

The setup/compiler boundary owns cross-field encounter validation.

At minimum:

- Partner ownership exists only in `PartnerDouble`.
- The Partner role must resolve to `Human` or `PartnerAI`.
- Ordinary living, non-active Wild opponent party members are invalid because
  they could enter through the generic mandatory-replacement path.
- An explicitly identified configured reinforcement may remain represented by
  the existing frozen scaffold, but this ADR does not activate, remove,
  refactor, or test that mechanic.
- Capture progression validates capture-domain rules independently from
  obedience. Capture badge counts above eight use the accepted neutral badge
  modifier; obedience retains its separate `0..8` rule.
- Invalid shapes fail atomically before `FBattleEngineState` is published.

The compiler must return a typed validation error that identifies the violated
policy family. It must not silently normalize an unsupported ownership or
controller shape into a different encounter.

### 3. Define thread ownership

All reads and writes to one `FBattleEngine` instance are caller-serialized.
Production callers use Unreal's game thread. Tests may use another single thread
only while exercising plain Battle-core/value logic; those tests must not touch
`UObject`, `AActor`, `UWorld`, UI, or other game-thread-only systems.

Concurrent access is unsupported, and the engine provides no internal locking.
State-version, action-identity, event-ordinal, and RNG-checkpoint checks detect
logical stale plans within serialized execution; they are not synchronization
primitives.

### 4. Define locked-action resolution checkpoints and stage fallible checkpoints

**Locked-action execution lifecycle** means every public mutation checkpoint
returning `FBattleResolution` whose accepted path starts, advances, resumes,
resolves, or completes the current `FBattleLockedAction`, regardless of which
public method receives the call. It includes `BeginNextLockedAction`, the
action-specific execution and Fight checkpoints, and decision-submission
branches that continue an already-current locked action, such as `PivotSwitch`.

Ordinary decision selection and locking before an action becomes current are
outside this lifecycle. Decision-sequence startup, end-turn resolution,
between-actions stat refresh, and other public mutators retain their declared
contracts unless their accepted path explicitly continues the current locked
action.

Every lifecycle-checkpoint invocation returns and appends the same
`FBattleResolution` exactly once. Each accepted checkpoint commits only its
owned deltas all-or-none. The appended resolution and its events appear exactly
once in authoritative resolution and event history.

A logical locked action may intentionally span multiple ordered checkpoints and
resolutions. All resource, RNG, state, event, action-progress, state-version,
and replay deltas owned by one checkpoint commit all-or-none. Facts committed
by an earlier checkpoint, such as PP before target resolution, remain committed
unless a rule explicitly defines compensation. Action completion and cursor
advancement occur only in the checkpoint that owns completion.

The explicit four-phase mechanism is required when a checkpoint could mutate a
resource, the parent RNG, or gameplay state before all fallible work owned by
that checkpoint is complete.

Current source confirms staged remediation is required for these locked-action
checkpoint families: `BeginNextLockedAction` held-item suppression; voluntary
`ExecuteCurrentSwitch`; `ExecuteCurrentWildAction` Run/WildFlee cleanup after
resolution RNG; Capture and status/confusion branches of
`ExecuteCurrentBagItem`; status/volatile gates in
`CommitCurrentMoveAfterPreMoveGates`; `ResolveCurrentMoveTargets`;
`ExecuteCurrentMoveEffects`; and `SubmitDecision`'s `PivotSwitch`
continuation. These are required remediation scope.

Audit every remaining lifecycle checkpoint. A branch proven to complete all
fallible work before mutation may remain mechanically unchanged.

When the explicit staged mechanism is required, it follows four phases:

1. **Validate**
   - Revalidate the current locked action, lifecycle checkpoint, owner, target,
     compiled policy, resource availability, catalog facts, capacity, and
     expected state identity.
   - Read only; no live mutation and no RNG.

2. **Prepare**
   - Construct immutable calculation inputs.
   - Complete every fallible deterministic arithmetic, shape, target, event,
     and mutation-plan calculation that can occur before randomness.
   - Read only; no live mutation and no RNG.

3. **Resolve and stage**
   - Resolve required random decisions through a resolution/checkpoint-scoped
     RNG transaction keyed by `FResolutionId` and correlated with the owning
     `FActionId`.
   - Build the complete typed result, staged state delta, resource delta,
     ordered events, and replay facts.
   - Neither staged draws nor staged mutations are visible through the parent
     engine.

4. **Commit**
   - Recheck the expected state version, locked-action identity, resolution
     identity, action cursor, and RNG transaction identity.
   - Commit the RNG transaction and apply the already complete staged deltas
     without another fallible gameplay operation.
   - Append ordered events, update action progress and state version, and append
     the same resolution exactly once as one externally observable transition.
   - Mark the action complete and advance the cursor only when this checkpoint
     owns completion.

A resolution commit plan is function-local to one synchronous lifecycle
checkpoint invocation. It must not retain raw pointers, `TConstArrayView`, or
references across a container mutation. It owns copied values and stable typed
IDs, then re-finds mutable state immediately before commit.

A stale lifecycle checkpoint may return and append one complete typed
cancellation or rejection resolution, including only its defined queue-control,
event, and replay facts. It must not consume an item, Bag quota, PP, gameplay
RNG, or publish a target mutation or success fact. A valid cancellation or
rejection is itself one atomic resolution transition, not rollback of a
partially published checkpoint.

### 5. Add transactional Battle RNG

The current `IBattleRandom::TryDrawUniform` immediately advances the parent
stream and trace. Capture may require a critical draw followed by a
data-dependent number of shake draws, so merely buffering copied trace entries
cannot provide rollback.

Battle randomness used during a staged lifecycle checkpoint must therefore use
a real resolution/checkpoint-scoped transaction keyed by `FResolutionId` and
correlated with the owning `FActionId`, a checkpoint/rollback contract, or an
equivalent atomic dynamic draw batch:

- Transactional draws advance only private working state.
- Destroying or rejecting the transaction leaves the parent stream, call
  ordinal, and exported trace unchanged.
- Commit succeeds only if the parent RNG identity/position, resolution
  identity, and owning action identity still match the transaction’s starting
  checkpoint.
- A successful commit advances the parent stream and publishes the ordered draw
  trace exactly once.
- Data-dependent early stopping remains supported; unused later shake draws are
  never consumed.

Transaction commit returns a typed failure reason. On success, the error value
is `None`. Every false return leaves the parent RNG state, call ordinal, and
exported trace unchanged.

The exact private type names may be refined during implementation, but drawing
directly from the parent RNG before the resolution checkpoint commit boundary
is forbidden for a fallible staged checkpoint.

### 6. Govern replay changes explicitly

`FBattleReplayRecord::CurrentSchemaVersion` is the canonical version source.

Storing compiled policy as derived runtime state does not by itself require a
wire-schema change: replay may serialize the immutable setup and compile the
same authority when reconstructing a new engine. If remediation changes any
serialized setup, event, snapshot, RNG, or final-result shape, the schema must
be appended deliberately.

Every schema bump requires:

- updating every exact-version assertion in affected older and current tests;
- checking outcome expectations changed by the same shared-state work;
- running every affected package filter;
- running the full `PokemonSolarus.Battle` suite; and
- accepting only exported report counters with no warnings, failures, not-run,
  or in-process entries.

Historical focused reports remain evidence for their original source state, not
for a later shared engine/replay state.

### 7. Preserve ADR-0001 and frozen boundaries

This ADR references but does not supersede ADR-0001. Battle core remains the
sole owner/writer of Battle state, and external systems continue receiving
observer-safe snapshots, typed requests, events, and final-result facts.

This decision does not change runtime DataTable composition, GameMode/HUD
presentation, Blueprint bindings, UI, assets, reward calculations, persistent
writes, or the frozen Cry/reinforcement scaffold.

### Architecture Diagram

```text
Authored FBattleSetupInput
          |
          v
setup validation + encounter-policy compilation
          |
          +----> immutable FBattleSetup
          |        configuration/replay input
          |
          +----> FBattleCompiledEncounterPolicies
                   sole runtime legality authority
                           |
                           v
                    FBattleEngineState
          caller-serialized; production game thread
                           |
              current locked typed Battle action
                           |
       public FBattleResolution lifecycle checkpoint
       start / advance / resume / resolve / complete
       includes SubmitDecision PivotSwitch continuation
                           |
        +------------------+--------------------+
        |                                       |
all fallible work complete       fallible work remains before
before mutation                  checkpoint-owned mutation
        |                                       |
existing compliant checkpoint        validate -> prepare
no mechanical refactor                         |
        |                       resolution-scoped RNG + staged delta
        |                                       |
        +------------------+--------------------+
                           |
              complete checkpoint result ready
                           |
         identity/checkpoint recheck when required
                  |                         |
       typed cancellation/rejection      valid commit
       defined control/event/replay      state + resources
       no gameplay/success facts         + RNG + events
                                        + progress/version/replay
                  |                         |
                  +------------+------------+
                               |
              same FBattleResolution returned
              and appended exactly once
                               |
              remains current for a later checkpoint
              or owning checkpoint completes/advances
```

### Key Interfaces

The names below describe required responsibilities. Exact private member
spelling may be refined without changing the decision.

```cpp
class FBattleEngineState
{
private:
    FBattleCompiledEncounterPolicies CompiledEncounterPolicies;
};

struct FBattleResolutionCommitIdentity
{
    FResolutionId ResolutionId;
    FActionId OwningActionId;
    uint64 ExpectedStateVersion;
    int32 ExpectedLockedActionIndex;
    uint64 ExpectedEventOrdinal;
    uint64 ExpectedRandomCallOrdinal;
};

struct FBattleResolutionCommitPlan
{
    FBattleResolutionCommitIdentity Identity;
    // Owned checkpoint-scoped resource, RNG, state, event, progress,
    // state-version, and replay deltas.
    // No retained pointers or non-owning views.
};

class IBattleRandomTransaction : public IBattleRandom
{
public:
    [[nodiscard]] virtual bool TryCommit(
        EBattleRandomTransactionCommitError& OutError) = 0;
    virtual void Rollback() = 0;
};
```

`TryCommit` sets `OutError` to `None` on success. Every false return provides a
typed reason and leaves the parent RNG state and trace unchanged.

The RNG transaction and resolution commit plan are scoped to one resolution
checkpoint. `FResolutionId` identifies that checkpoint, and `FActionId`
correlates it with the owning logical action.

The implementation must not copy the whole `FBattleEngineState` merely to gain
rollback. The state owns move-only resources such as
`TUniquePtr<IBattleRandom>`. Use bounded typed deltas plus a distinct RNG
transaction.

## Alternatives Considered

### Alternative 1: Keep raw setup policy as runtime state and add targeted guards

- **Description**: Patch the identified Wild, Partner, and Capture cases while
  runtime helpers continue reading raw setup fields.
- **Pros**: Smallest immediate source diff.
- **Cons**: Keeps two policy representations with no authoritative one, repeats
  encounter logic across helpers, and makes future drift likely.
- **Rejection Reason**: It repairs symptoms without establishing the compiled
  policy boundary C09 was intended to provide.

### Alternative 2: Mutate live state and roll back selected fields on failure

- **Description**: Preserve the existing order and restore Bag, quota, trigger,
  or target fields when a later step fails.
- **Pros**: Can be introduced locally around Capture.
- **Cons**: Every new mutation must be remembered; RNG stream state, event
  ordinals, trigger state, action cursor, and replay facts are easy to omit.
  Copying the entire engine state is also unsuitable because of move-only
  ownership.
- **Rejection Reason**: Ad hoc rollback cannot reliably prove all-or-none
  behavior across the Battle engine.

### Alternative 3: Treat every post-validation resolver failure as fatal

- **Description**: Continue assuming that earlier input validation guarantees
  every later operation and terminate if that assumption fails.
- **Pros**: Keeps success paths simple.
- **Cons**: Current validation demonstrably accepts an input that can fail later.
  Fatal logging terminates the game and does not restore already-consumed
  resources. Assertions also do not provide Shipping recovery.
- **Rejection Reason**: Valid authored or runtime boundary failures require a
  typed fail-closed result, not a process-ending recovery path.

## Consequences

### Positive

- Runtime encounter legality has one explicit, inspectable authority.
- Invalid C09 shapes fail before mutable Battle state exists.
- Resource, RNG, event, and replay facts cannot disagree after an aborted
  checkpoint.
- Legal unsuccessful actions retain their intended canonical consumption.
- Multi-checkpoint Fight actions preserve earlier committed facts, including PP
  committed before target resolution.
- Capture no longer needs fatal logging for a recoverable calculation boundary.
- Replay schema updates receive current cross-package validation.
- Locked-action lifecycle checkpoints share one atomic resolution invariant and
  can reuse the staged mechanism when fallible work precedes checkpoint-owned
  mutation.

### Negative

- The RNG interface and its deterministic test implementations need a real
  transaction/checkpoint capability.
- Affected resolution checkpoints gain bounded plan/delta structures and a
  commit step.
- Runtime consumers of raw encounter policies must migrate to compiled policy.
- Older tests with stale schema or outcome assertions must be updated and run.
- The atomicity guarantee excludes process-level allocation failure; Unreal
  containers do not provide recoverable out-of-memory transactions.

### Risks

- **Over-broad refactor**: Rewriting an entire multi-checkpoint action lifecycle
  could expand the C09 remediation. Mitigation: remediate the confirmed
  checkpoint set above through bounded checkpoint-local plans, audit every
  remaining lifecycle checkpoint independently, and leave proven-compliant
  branches mechanically unchanged.
- **Unsupported concurrent access**: A caller could invoke one engine instance
  from multiple threads and mistake logical stale-plan checks for
  synchronization. Mitigation: document caller-serialized access, require the
  game thread in production, and provide no implicit thread-safety claim.
- **RNG transaction divergence**: Parent and transaction streams could commit
  out of order. Mitigation: bind the transaction to `FResolutionId`, the owning
  `FActionId`, the parent checkpoint, and expected call ordinal, and reject
  stale commits.
- **Cross-checkpoint rollback**: A later Fight checkpoint could incorrectly
  undo PP or other facts committed by an earlier checkpoint. Mitigation: treat
  earlier resolutions as committed history unless a Battle rule explicitly
  defines a compensating transition.
- **Stale plan references**: `TArray` mutation may invalidate pointers or views.
  Mitigation: plans own copied values and stable IDs and re-find mutable state
  only at commit.
- **Ordering drift**: Rebuilding events or draws through unordered containers
  can alter replay. Mitigation: retain canonical order in `TArray`; do not use
  `TMap` or `TSet` iteration as serialized order.
- **Frozen-mechanic expansion**: Wild-reserve validation could accidentally
  alter Cry/reinforcement. Mitigation: explicitly exempt only the existing
  configured reinforcement identity and leave its setup/state/replay scaffold
  unchanged.
- **False compliance claim**: Writing the ADR does not prove current code
  complies. Mitigation: keep the ADR Proposed until independent review, then
  implement and validate separately.

## GDD Requirements Addressed

| GDD System | Requirement | How This ADR Addresses It |
|------------|-------------|--------------------------|
| `design/gdd/game-concept.md` — Pillar 2 | Battles follow familiar rules with explicit Solarus exceptions. | One compiled policy turns the approved encounter rules into the sole runtime authority. |
| `design/gdd/game-concept.md` — Pillar 4 | Build the smallest reusable seam needed now without speculative systems. | Bounded resolution commits remediate the confirmed checkpoint set above; audited branches already completing fallible work before mutation remain mechanically unchanged. |
| `plan/battle_mechanics/05-actions-order-and-targeting.md` — Fight order | Normal target resolution occurs only after move commit and PP spend; a no-target result keeps spent PP. | Checkpoint atomicity preserves the earlier PP resolution while later target/effect checkpoints commit their own deltas independently. |
| `docs/battle-system-interview-handoff.md` — Wild and partner rules | Wild opponents do not use ordinary party switching; partners retain separate ownership and resolve to player or Partner AI control. | Invalid Wild reserves and Partner/controller shapes fail before engine creation. |
| `docs/battle-system-interview-handoff.md` — Capture | A blocked capture consumes nothing; a legal failed capture consumes its Ball and action. | Validation/abort and legal gameplay failure receive distinct atomic outcomes. |
| `plan/battle_mechanics/10-encounters-capture-escape-and-partner.md` | Setup compiles deterministic encounter policies; C09 flows replay identically. | The compiled object becomes runtime authority, while atomic state/RNG/event commits preserve deterministic replay. |

## Performance Implications

- **CPU**: One bounded policy compilation per engine instance and small
  checkpoint-local preparation/commit work on affected paths. No per-frame
  system is introduced.
- **Memory**: One compiled policy value plus bounded resolution deltas and
  temporary RNG transaction state where staging is required. Party size remains
  six and active positions remain at most four.
- **Load Time**: None beyond existing setup creation.
- **Network**: None. Solarus remains single-player.

## Migration Plan

1. Keep this ADR Proposed and rerun an independent `/architecture-review`
   after these revisions.
2. After acceptance, transfer one successful compiled encounter policy into
   `FBattleEngineState` and migrate runtime policy consumers.
3. Add typed setup/compiler failures for ordinary Wild reserves and invalid
   Partner controller modes without altering the frozen reinforcement scaffold.
4. Separate capture badge validation from obedience badge validation.
5. Add resolution/checkpoint-scoped transactional RNG keyed by `FResolutionId`,
   correlated with the owning `FActionId`, and covered by typed commit failure
   and rollback proof for the production seeded stream and deterministic test
   sources.
6. Introduce private bounded `FBattleResolutionCommitPlan` values for the
   confirmed checkpoint set above.
7. Remediate `BeginNextLockedAction` held-item suppression, voluntary
   `ExecuteCurrentSwitch`, `ExecuteCurrentWildAction` Run/WildFlee, and Capture
   and status/confusion branches of `ExecuteCurrentBagItem` so all fallible
   preparation and resolution-scoped RNG precede live mutation.
8. Remediate status/volatile gates in `CommitCurrentMoveAfterPreMoveGates`,
   `ResolveCurrentMoveTargets`, `ExecuteCurrentMoveEffects`, and
   `SubmitDecision`'s `PivotSwitch` continuation so each checkpoint commits only
   its owned deltas all-or-none.
9. Preserve checkpoint-specific gameplay semantics, including Capture
   consumption and outcomes, ordered voluntary/Pivot entry effects,
   Run/WildFlee outcomes, and earlier Fight PP commits.
10. Audit every remaining locked-action lifecycle checkpoint for the same
    atomic-resolution invariant. Any additional noncompliant checkpoint joins
    required remediation and validation scope; a proven-compliant branch remains
    mechanically unchanged.
11. Update every stale replay-schema and Partner Team Victory expectation.
12. Add the missing focused C09 and cross-package regression proofs.
13. Run the affected filters and full `PokemonSolarus.Battle` suite, judging
    acceptance from exported `index.json` counters.

## Validation Criteria

- Engine state stores a valid `FBattleCompiledEncounterPolicies` value and
  runtime encounter checks no longer read raw authored permission fields.
- The public engine access contract states that one instance is
  caller-serialized, production access uses Unreal's game thread, and
  concurrent access is unsupported.
- The locked-action execution lifecycle includes every
  `FBattleResolution`-returning accepted path that starts, advances, resumes,
  resolves, or completes the current `FBattleLockedAction`, including
  `SubmitDecision`'s `PivotSwitch` continuation.
- Ordinary pre-current decision selection/locking, decision-sequence startup,
  end-turn resolution, and between-actions stat refresh remain outside this
  lifecycle unless their accepted path explicitly continues the current action.
- Every lifecycle-checkpoint invocation returns and appends the same
  `FBattleResolution` exactly once. Each accepted checkpoint commits only its
  owned deltas all-or-none.
- A logical locked action may span ordered checkpoints. Earlier checkpoint
  facts, including Fight PP committed before target resolution, remain
  committed absent an explicit compensation rule.
- Action completion and cursor advancement occur only in the checkpoint that
  owns completion.
- Every checkpoint in the confirmed remediation set above uses the explicit
  four-phase mechanism and has focused failure-path proof that pre-commit
  failure publishes no checkpoint-owned success delta or parent RNG and
  returns and appends only its defined typed rejection transition.
- Every remaining lifecycle checkpoint is audited. A branch proven to complete
  all fallible work before mutation may remain mechanically unchanged.
- Equivalent setup and seed compile the same policy and reproduce identical
  runtime requests, events, snapshots, RNG trace, and replay.
- Unsupported encounter/format/role/controller combinations fail before engine
  creation with typed errors.
- Every ordinary living Wild reserve not covered by the frozen configured
  reinforcement identity is rejected.
- Partner controllers other than `Human` or `PartnerAI` are rejected.
- Capture badge counts above eight receive the neutral capture badge modifier;
  obedience retains its separate validation.
- A capture checkpoint whose deterministic calculation cannot resolve commits
  no Ball, Bag quota, gameplay RNG, gameplay state, or success fact and does not
  terminate the process. It returns and appends one typed rejection resolution
  containing only its defined rejection event and replay facts.
- A PivotSwitch checkpoint whose entry-item, hazard, or Ability preparation
  cannot resolve publishes no switch, entry result, action completion, cursor
  advancement, gameplay RNG, success event, or success replay fact. It returns
  and appends one typed rejection resolution.
- Rolling back after one or more staged draws leaves the parent RNG state,
  call ordinal, and exported trace unchanged.
- A stale lifecycle checkpoint returns one typed rejection resolution, and its
  RNG transaction returns a typed failure and cannot commit.
- Every false RNG transaction commit leaves the parent RNG state, call ordinal,
  and exported trace unchanged; success reports `None`.
- A legal unsuccessful capture consumes exactly one Ball, one Trainer Bag
  action, and only the required early-stopping draws.
- A successful capture commits its pending record, removal, queue
  cancellations, destination, events, and outcome atomically.
- Every exact schema assertion agrees with
  `FBattleReplayRecord::CurrentSchemaVersion`.
- Partner Team Victory regression expectations agree with the typed 1-HP/status
  recovery contract.
- C09A, C09B, C09C, every affected older filter, and the full
  `PokemonSolarus.Battle` suite export zero-warning, zero-failure,
  zero-not-run, and zero-in-process reports.

## Related Decisions

- `docs/architecture/adr-0001-data-driven-battle-runtime-and-fail-closed-hud.md`
- `docs/registry/architecture.yaml`
- `design/gdd/game-concept.md`
- `docs/battle-system-interview-handoff.md`
- `plan/battle_mechanics/00-roadmap-index.md`
- `plan/battle_mechanics/05-actions-order-and-targeting.md`
- `plan/battle_mechanics/10-encounters-capture-escape-and-partner.md`
- `plan/battle_mechanics/reference/modern-rules-snapshot.md`
