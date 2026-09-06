# Pokemon Solarus — Codex Repository Instructions

## Core Rule

The final Solarus vision defines where the project is going.

The **current playable milestone** defines what is allowed to be built now.

Future requirements may influence a clean replacement or extension boundary, but
they must not create present implementation, abstraction, testing, validation,
documentation, or architecture work unless the current milestone requires it.

Do not implement a future system merely because it is already planned.

Existing completed systems, especially BattleEngine, should be treated as a
stable library by default. Change them only when the active playable milestone
demonstrates a specific need.

## Project

* The authoritative Unreal project is `Game/PokemonSolarus.uproject`.
* Engine: Unreal Engine 5.8.1.
* Primary implementation language: C++.
* Read `production/session-state/active.md` to determine the current development
  mode, milestone, scope, exclusions, and next action.
* Use `index.md` to route repository reading.
* Do **not** read the whole repository before beginning work.
* Search first, then read only the files directly relevant to the current task.
* Read `docs/code-file-organization.md` before planning, creating, or modifying
  hand-authored code or automated tests.
* Read `docs/registry/external-assets.yaml` only when third-party asset use,
  import, provenance, or distribution is relevant.
* Historical plans, reports, ADRs, and evidence are not default context. Read
  them only when the current task depends on them.

## Development Modes

Solarus uses three modes.

### SPIKE

Use for one uncertain technical or design question.

* Optimize for learning.
* Temporary or ugly work is acceptable.
* Placeholder or hardcoded data is acceptable when bounded to the experiment.
* Use minimal validation.
* Do not create production architecture for a spike.
* Dispose of spike work or explicitly promote it through a later task.

### PLAYABLE

This is the default mode unless the user says otherwise.

Optimize for the next player-visible result.

* Build the smallest implementation that makes the current milestone playable.
* Keep code reasonably reusable, but do not build speculative frameworks.
* Prefer one obvious future replacement seam over implementing future systems.
* Use focused automated tests for changed behavior.
* Build and test in Unreal when relevant.
* Player-facing work requires real PIE verification where applicable.
* Do not require full production evidence, exhaustive regression, independent
  reviews, or broad architecture work by default.
* Stop once the approved player-visible result works and its relevant validation
  passes.

### PRODUCTION

Use only when explicitly selected by the user, required by a current accepted
authority, or justified by genuine production/release risk.

Production mode may require:

* formal implementation drafts;
* independent approval;
* architecture review;
* exhaustive regression;
* code review;
* test-evidence review;
* migration or compatibility proof;
* release evidence.

Do not enter Production mode simply because stronger validation is possible.

## Playable Workflow

For ordinary PLAYABLE work, use:

**Goal -> Decision if needed -> Implement -> Focused Validation -> PIE -> Accept or Fix**

If behavior is already settled by an accepted design, do not redesign it.

If the user directly asks to implement an already-approved bounded scope, that
request is implementation authorization for that scope. Do not insert another
formal implementation-draft and independent-approval cycle unless required by a
current authority or a real architecture trigger.

If implementation discovers that the approved scope must materially expand,
stop that expansion and report the specific reason.

Do not automatically invoke another workflow or skill because the previous one
recommended it.

## Future-Feature Barrier

A future requirement is not present work.

Examples:

* Future strategic AI does not justify building a general AI framework now.
* Future hundreds of Pokemon do not justify a complete learnset system now.
* Future special effects do not justify implementing unsupported effects now.

A future requirement may justify leaving a clean extension point. Nothing more
is required until that feature becomes part of the current or immediately next
playable milestone.

## Architecture and Review Rules

Create or change an ADR only when:

1. a current accepted authority explicitly requires one; or
2. the current implementation genuinely changes a fundamental boundary such as:

   * authoritative state ownership;
   * persistence or save format;
   * module ownership;
   * a broadly consumed public/reflected contract;
   * transactional RNG ownership;
   * replay/event publication semantics;
   * lifecycle or atomicity guarantees whose incorrect design would affect the
     current milestone.

Do not create an ADR merely because a cleaner or more general architecture can
be imagined.

Architecture and code reviews may block current work only for:

* a demonstrated correctness defect;
* violation of a current approved requirement;
* data corruption or loss risk;
* security or serious stability risk;
* an architectural defect that blocks the current or immediately next
  milestone.

Speculative future extensibility concerns are advisory. Record them as technical
debt instead of blocking current work.

Default review limit:

1. review once;
2. apply required corrections;
3. perform one confirmation review if needed.

Further review/fix loops require a concrete unresolved blocker from the list
above. Do not continue cycling on advisory findings.

## Collaboration and Ownership

* Preserve unrelated dirty work.
* Do not rewrite or delete user changes outside the approved task.
* Do not commit, push, stage, create branches, reset, checkout, or alter Git
  history unless the user explicitly asks.
* The user exclusively owns and approves frontend/UI/UX design, visual design,
  art, layout, styling, materials, textures, composition, motion appearance,
  audio treatment, and related reference assets.
* Do not reopen, assess, redesign, or modify those owner decisions unless the
  user explicitly requests that exact work or a current functional requirement
  creates a concrete conflict that must be reported.
* Do not call art, UX-design, UI-design, audio-design, technical-art, or other
  presentation-design subagents unless the user explicitly requests that work.
* Do not read visual-reference material merely to review the user's visual
  choices.
* Functional UI code-behind is not visual design. Agents may inspect and modify
  mechanics, state, input routing, data, adapters, validation, bindings, and
  automated tests when those are part of the approved task.
* Codex must not alter user-owned visual assets without a task-specific
  exception.

The current Battle keyboard mapping is a closed owner decision:

* Arrow keys navigate.
* `C` is Confirm.
* `X` is Cancel.
* `V` is Battle Info.

Do not reopen these bindings unless the user explicitly does so.

## Battle Work

For Battle tasks:

* The active playable milestone controls scope.
* Use the current accepted task authority as the behavior contract.
* Reuse existing BattleEngine, catalog, runtime, decision, replay, and
  presentation boundaries instead of rebuilding them.
* Do not perform unrelated BattleEngine cleanup while implementing a playable
  feature.
* Do not close deferred catalog gaps merely because they exist.
* Do not add species-, move-, or showcase-specific branches to generic runtime
  systems.
* When BattleEngine must change, preserve its established authoritative state,
  transactional RNG ownership, event/replay ordering, and stale-identity
  guarantees unless the current approved design explicitly changes them.
* Run only validation relevant to the changed responsibility. Broader regression
  is required only when the shared code changed in a way that can affect those
  older behaviors.
* Native tests do not replace real Blueprint or actual-size PIE acceptance when
  the current milestone requires player-facing proof.

If the current accepted GDD explicitly requires an ADR, complete that ADR before
the affected implementation. Keep the ADR strictly bounded to the current
milestone; do not use it to design later roadmap systems.

## Context and Token Discipline

Before reading a file, ask whether it can materially affect the current task.

Default starting context:

1. `AGENTS.md`;
2. `production/session-state/active.md`;
3. the relevant section of `index.md`;
4. the current task's accepted authority;
5. directly relevant implementation and tests.

Do not recursively read every linked document.

Do not read entire directories when repository search or the index identifies a
smaller relevant set.

Do not reload historical evidence merely to prove facts already recorded as
accepted unless the current task challenges or depends on that evidence.

## Validation Discipline

Use the smallest validation that can prove the current change.

* Local feature change: focused tests + relevant build + PIE when player-facing.
* Shared Battle core change: focused tests + affected older regression filters.
* Completed playable milestone: milestone regression and one broader review.
* Production/release work: use the formal production evidence required by that
  task.

Do not run or analyze the entire Battle suite after every local feature change.

## Safety

* Use `rg` / `rg --files` for discovery.
* Use `apply_patch` for hand-authored file edits.
* Do not modify generated Unreal folders such as `Game/Intermediate`,
  `Game/Binaries`, or `Game/Saved` except while running approved validation.
* Git publication is always separately authorized.

## Model and Reasoning Recommendation

Before substantial work, briefly recommend the weakest model/reasoning
configuration that is sufficient for the task.

Default ladder:

* **Luna / Light** — discovery, repository search, mechanical edits, formatting,
  simple documentation, and straightforward log/test inspection.
* **Terra / Medium** — normal implementation, routine debugging, tests,
  refactoring, and moderately complex multi-file work.
* **Sol / High** — difficult implementation, complex debugging,
  cross-subsystem changes, architecture-sensitive work, and substantial review.
* **Astra / Max** — reserve for the hardest ambiguous end-to-end tasks, major
  architecture decisions, unresolved problems after a serious Sol attempt, or
  critical independent review of high-impact changes.

Reasoning may be adjusted independently:

* Light — obvious/mechanical work.
* Medium — normal multi-step work.
* High — difficult dependencies or debugging.
* Extra High — when High is demonstrably insufficient.
* Max — only when maximum reasoning is genuinely justified.

State the recommendation briefly as:

`Recommended: <Model> / <Reasoning>`

The recommendation is advisory. Do not stop, restart, or switch models unless
the user asks.
