# Pokemon Solarus Workflow Guide

## Purpose

This guide helps the user and Codex select the right workflow for project work.
It is a routing guide, not implementation authority. It does not grant
permission to edit files, assets, tests, configuration, documentation, or Git
history.

Every meaningful change follows:

**Question -> Options -> Decision -> Draft -> Approval -> Implementation ->
Validation -> Review -> Acceptance**

Tests, builds, and reviews are evidence. They do not replace owner approval.

## Agent usage contract

Read this guide when the user:

- does not know what to do next;
- asks which workflow or skill to use;
- proposes adding, upgrading, reviewing, or removing a feature;
- requests work spanning multiple disciplines; or
- asks whether the project is ready to advance.

This guide selects a workflow. Before using a selected skill, read its complete
`.codex/skills/<skill-name>/SKILL.md`. Do not infer a skill's current behavior
from this summary alone.

## Authority order

When instructions conflict, follow this order:

1. Higher-priority system, developer, and current user instructions.
2. `AGENTS.md`.
3. User-named current authorities, in the order the user names them.
4. The live roadmap package for package work.
5. Live source, tests, worktree state, and fresh exported evidence.
6. `production/session-state/active.md`.
7. Accepted design and architecture documents.
8. This workflow guide.
9. Generic skill defaults.
10. Historical reports and prior-session notes.

If current authorities genuinely conflict, stop and show the conflict. Do not
guess which behavior should win.

## Agent read order

Before recommending or starting work:

1. Read `AGENTS.md`.
2. Read `production/session-state/active.md`.
3. Inspect `git status --short` and protect unrelated dirty work.
4. Read user-named authorities in their stated order.
5. Read the live roadmap package when applicable.
6. Use this guide to select one primary workflow.
7. Read the selected skill's complete `SKILL.md`.
8. Read directly relevant source, tests, and fresh evidence.
9. Read `docs/code-file-organization.md` before planning or changing
   hand-authored code or automated tests.
10. Read `docs/registry/external-assets.yaml` before proposing third-party
    asset use or import.

## Workflow selection algorithm

The agent must:

1. Establish the requested outcome and current project state.
2. Determine the task type and whether it is prototype or production work.
3. Select exactly one primary workflow from this guide.
4. State the first action only; show later steps as brief context.
5. Identify required decisions, approvals, evidence, and exclusions.
6. Never auto-run the next skill merely because another skill recommends it.
7. Say what is unknown when evidence is insufficient.
8. Stop when a user-owned decision or new authorization is required.

When several workflows overlap, begin with the earliest unresolved decision.
For example, do not start implementation planning while game behavior remains
undecided, and do not schedule an unapproved implementation draft.

## Standard agent response when the user is unsure

Use this compact form:

```text
Current situation:
[Verified project state]

Recommended next action:
[Exactly one action or skill]

Not yet authorized:
[Implementation, assets, Git operations, or later work]

After that:
[Short preview of later steps]

Unknowns:
[Anything the available evidence cannot establish]
```

Do not overwhelm the user with every possible later skill. Give one primary
recommendation.

## Universal Solarus implementation path

Any task that may change hand-authored code, automated tests, source data,
Unreal assets, or configuration must eventually use this path:

1. Reach a clear design decision.
2. Run `/solarus-implementation-draft`.
3. Explicitly select `prototype` or `production` mode. Do not infer the mode.
4. Run an independent `/solarus-implementation-approval` review.
5. Resolve every `REVISE` or `BLOCK` verdict.
6. Present the exact write set, exclusions, validation, dirty paths, and stop
   conditions.
7. Obtain explicit implementation authorization.
8. Implement only the approved scope.
9. Run the approved validation.
10. Run `/code-review` and `/test-evidence-review` when required.
11. Obtain separate final acceptance.

Draft approval does not authorize implementation. Implementation approval does
not authorize staging, committing, pushing, branching, or altering history.

## Quick router

| The user wants to... | Start with |
|---|---|
| Find the next project task | `/help` |
| Audit the whole project stage | `/project-stage-detect` |
| Start using the workflow in an existing project | `/adopt` |
| Recover missing documents from implementation | `/reverse-document` |
| Add a small mechanic or adjustment | `/quick-design` |
| Add a large feature or system | `/design-system` |
| Upgrade an existing feature | Update its design, then `/propagate-design-change` |
| Test a risky idea quickly | `/prototype --spike` |
| Add or audit gameplay content | `/content-audit` |
| Fix a bug | `/bug-report` |
| Make or import art | `/asset-spec` |
| Design UI or HUD behavior | `/ux-design` |
| Improve measured performance | `/perf-profile` |
| Review security | `/security-audit` |
| Prepare a release | `/release-checklist` |
| Continue efficiently in another session | `/new-session` |

## Feature workflows

### Small feature or tuning change

**Use when:** The change is a narrow addition, number adjustment, or existing
rule change that does not require a full system GDD.

**Sequence:**

`/quick-design` -> optional `/design-review` -> Solarus implementation path ->
focused validation -> final acceptance

If balance is affected, finish with `/balance-check` and focused playtesting.

### Major new feature

**Use when:** The feature introduces substantial game behavior, a new system,
or several connected responsibilities.

**Sequence:**

`/design-system` -> `/design-review` -> architecture work when required ->
epic/story or live roadmap package -> `/story-readiness` -> Solarus
implementation path -> `/story-done`

Use `/architecture-decision` when the feature changes state ownership, public
or reflected contracts, persistence, modules, transactions, RNG, lifecycle, or
event/snapshot/replay order.

### Upgrade an existing feature

1. Determine whether the existing GDD authorizes the changed behavior.
2. Use `/quick-design` for a narrow extension or revise the system GDD for a
   rule change.
3. Run `/design-review`.
4. Run `/propagate-design-change` after changing a GDD.
5. Review affected ADRs, epics, stories, tests, and consumers.
6. Enter the Solarus implementation path.
7. Rerun affected older and new regression tests.
8. Obtain final acceptance.

### Risky experiment or spike

**Use when:** One uncertain design or technical question should be answered
before committing to maintained implementation.

**Sequence:**

`/prototype --spike` -> define one question -> bound the experiment -> observe
the result -> decide to abandon, investigate, or enter a normal feature workflow

For changes inside the Solarus project, use
`/solarus-implementation-draft` in `prototype` mode before modifying files. A
spike must not silently become production code.

### Production-quality vertical slice

**Use when:** Approved GDDs, architecture, and UX specifications exist, but the
complete game loop still needs an end-to-end production-quality feasibility
check before full production commitment.

**Sequence:**

define the slice and success criteria -> `/vertical-slice` -> implement only
the approved end-to-end slice -> playtest -> record `PROCEED`, `PIVOT`, or
`KILL` evidence -> owner decision -> `/gate-check`

A vertical slice is not the same as a throwaway prototype. It tests whether the
designed production pipeline and complete loop can work together.

## Battle package workflow

For Battle work, the live roadmap package is the implementation contract:

roadmap package -> Solarus implementation draft -> independent approval ->
implementation authorization -> named package filter -> affected older package
filters -> exported `index.json` inspection -> reviews -> independent final
acceptance

Preserve:

- one authoritative Battle state, transaction, and RNG owner;
- legality, targeting, resource, damage, event, snapshot, replay, and
  publication order;
- observer-safe information and stale-identity checks;
- generic systems without Pokemon-, move-, match-, or scenario-specific
  branches; and
- existing public and reflected contracts unless a change is explicitly
  designed and approved.

Run only the named package filter unless the user expands scope. After a shared
BattleEngine or executor change, rerun affected older package filters. Judge
Automation through exported `index.json`, not the process exit code alone.

## Content and data workflow

**Use for:** Pokemon, moves, Abilities, items, conditions, encounters, and other
authored gameplay content.

**Sequence:**

`/content-audit` -> confirm design authority -> `/quick-design` if new rules are
needed -> Solarus implementation path -> pure source validation -> approved
import -> catalog/runtime equivalence -> affected mechanic tests ->
`/balance-check` -> acceptance

Validate source data before mutating Unreal assets. Do not import or use a
third-party asset unless `docs/registry/external-assets.yaml` approves it.

## Art and presentation workflows

### Art assets

`/art-bible` -> `/asset-spec` inventory and per-asset specification -> visual
approval -> production/import -> `/asset-audit` -> in-engine verification

The user owns layout, styling, artwork, materials, textures, composition, and
motion appearance. Codex may handle contracts, loading, state, bindings,
validation, and code integration unless the user grants a task-specific visual
exception.

### UI and HUD

`/ux-design` -> `/ux-review` -> define state/events/input/bindings -> Solarus
implementation path -> code-behind -> user-created or user-approved visuals ->
Blueprint compilation -> actual-size PIE -> review and acceptance

Native tests do not prove real Blueprint delivery. `/team-ui` must preserve the
user's ownership of visual decisions.

### Audio

approved direction -> `/team-audio` -> audio asset requirements -> production
-> gameplay integration -> validation -> asset audit

### Levels and areas

requirements -> `/team-level` -> level specification -> asset and mechanic
plans -> implementation -> user visual approval -> playtesting -> QA

### Narrative

narrative requirements -> `/team-narrative` -> `/design-review` -> stories ->
implementation -> consistency review -> localization

## Bug and maintenance workflows

### Normal bug

`/bug-report create` or `analyze` -> reproduce -> establish root cause -> minimal
Solarus implementation path -> regression test -> reviews ->
`/bug-report verify` -> `/bug-report close`

Use `/bug-triage` when multiple bugs compete for attention.

### Emergency hotfix

**Use only for:** A serious released-build problem that requires expedited work.

`/bug-report` -> `/hotfix` -> rollback plan -> minimum fix -> smoke or focused
QA -> deployment approval -> production verification -> `/retrospective hotfix`

The hotfix skill may propose a Git branch. It cannot create one without explicit
Git authorization.

### Day-one patch

gold master -> select safe P1/P2 fixes -> `/day-one-patch` -> rollback plan ->
minimal fixes -> targeted QA -> `/patch-notes` -> deployment verification ->
retrospective

Do not add features or refactor during a day-one patch.

### Technical debt or refactoring

`/tech-debt scan` -> prioritize -> schedule -> architecture decision if
ownership changes -> Solarus implementation path -> regression validation

Separate mechanical relocation from behavior changes. Follow
`docs/code-file-organization.md`.

## QA workflows

### Test infrastructure

Use only when genuinely missing:

`/test-setup` -> `/test-helpers`

Solarus already has Unreal Automation infrastructure. Do not replace it with a
generic test scaffold without a separately approved plan.

### Feature or sprint QA

`/qa-plan` -> automated and manual tests -> `/smoke-check` ->
`/regression-suite` -> `/test-evidence-review` -> `/team-qa`

Use `/test-flakiness` only when repeated test history exists. Use `/soak-test`
for long-session stability. Native tests do not replace required real Blueprint
or actual-size PIE acceptance.

## Quality improvement workflows

### Balance

`/quick-design` -> approve the intended result -> implement ->
`/balance-check` -> playtest -> adjust through a new approval cycle

### Performance

`/perf-profile` -> record a baseline -> identify a measured bottleneck ->
`/scope-check` -> `/estimate` -> Solarus implementation path -> remeasure under
the same conditions -> regression validation

Do not optimize without a reproducible baseline.

### Security

`/security-audit` -> classify findings -> bugs or stories -> Solarus
implementation path -> security retest -> release or gate review

### Localization

`/localize scan` -> `extract` -> `freeze` -> `brief` -> translation ->
`validate` -> cultural review -> RTL/VO checks when applicable -> localization
QA

The generic localization skill contains non-Unreal path assumptions. Adapt it
to the project's current Unreal localization structure through an approved
draft before allowing mutation.

## Production management workflows

### Sprint cycle

`/retrospective` -> `/sprint-plan` -> `/scope-check` -> `/story-readiness` ->
implementation cycles -> `/sprint-status` -> `/smoke-check` -> retrospective

Use `/estimate` before accepting unusually risky stories.

### Milestone review

`/sprint-status` -> `/content-audit` -> `/milestone-review` -> resolve blockers
-> `/gate-check`

A gate verdict is evidence and advice. The user decides whether to advance.

### Polish

`/perf-profile` -> `/balance-check` -> `/asset-audit` -> multiple
`/playtest-report` sessions -> `/tech-debt` decision -> `/soak-test` ->
`/team-polish` -> gate review

### Release

`/release-checklist` -> security and localization clearance ->
`/launch-checklist` -> explicit GO/NO-GO -> `/team-release` -> `/patch-notes`
and `/changelog` -> deployment -> monitoring -> retrospective

### Live operations

`/team-live-ops` -> event design -> economy and analytics plans -> narrative and
content -> design review -> sprint plan -> implementation and QA -> release
workflow

## Project and session workflows

### Existing project recovery

`/start` -> `/project-stage-detect` -> `/adopt` -> `/reverse-document` where
needed -> `/gate-check`

### Contributor onboarding

Use `/onboard [role or area]` to prepare focused project context for a new
contributor or agent.

### Fresh session

Use `/new-session` to create one verified, paste-ready continuation prompt with
one bounded objective. Prefer it over the older temporary `/handoff` workflow.

### Live Unreal Editor

Use `/unreal-engine-mcp-codex` for Blueprint or asset inspection, Content
Browser operations, level and actor work, live editor state, and PIE-specific
inspection.

Use command-line builds and Automation for compilation and tests. Serialize
live-editor calls, inspect every result, and save only assets owned by the task.

### Skill maintenance

`/skill-test static` -> `/skill-test spec` -> `/skill-test audit` ->
`/skill-improve` -> rerun the same validation

This maintains the workflow system, not game features.

## Supporting decision and review skills

These assist other workflows but usually do not define a complete lifecycle:

- `/help` selects one next action from current evidence.
- `/gate-check` assesses phase-transition readiness.
- `/estimate` estimates bounded work and uncertainty.
- `/scope-check` detects growth beyond an approved boundary.
- `/sprint-status` reports current sprint state.
- `/milestone-review` reports milestone state and risk.
- `/content-audit` compares planned and implemented content.
- `/consistency-check` finds contradictions between design authorities.
- `/review-all-gdds` reviews the design set as a whole.
- `/code-review` reviews implemented code quality and architecture.
- `/test-evidence-review` reviews test and evidence quality.
- `/playtest-report` structures observed playtest results.

## Skill families

### Navigation, recovery, and continuity

`/start`, `/help`, `/project-stage-detect`, `/adopt`, `/reverse-document`,
`/onboard`, `/new-session`, `/handoff`

### Concept and design

`/brainstorm`, `/art-bible`, `/map-systems`, `/prototype`, `/design-system`,
`/quick-design`, `/design-review`, `/consistency-check`, `/review-all-gdds`,
`/propagate-design-change`, `/vertical-slice`

### Architecture

`/setup-engine`, `/create-architecture`, `/architecture-decision`,
`/architecture-review`, `/create-control-manifest`

### Planning and production

`/create-epics`, `/create-stories`, `/story-readiness`, `/estimate`,
`/sprint-plan`, `/sprint-status`, `/scope-check`, `/dev-story`, `/story-done`,
`/retrospective`, `/milestone-review`

### Solarus implementation gates and review

`/solarus-implementation-draft`, `/solarus-implementation-approval`,
`/code-review`, `/test-evidence-review`

### Content, presentation, and domain teams

`/content-audit`, `/asset-spec`, `/asset-audit`, `/ux-design`, `/ux-review`,
`/team-combat`, `/team-ui`, `/team-audio`, `/team-level`, `/team-narrative`,
`/team-live-ops`

### Bugs, QA, and maintenance

`/bug-report`, `/bug-triage`, `/hotfix`, `/day-one-patch`, `/test-setup`,
`/test-helpers`, `/qa-plan`, `/smoke-check`, `/regression-suite`,
`/test-flakiness`, `/soak-test`, `/team-qa`, `/tech-debt`

### Quality, polish, and release

`/balance-check`, `/perf-profile`, `/security-audit`, `/localize`,
`/playtest-report`, `/team-polish`, `/release-checklist`, `/launch-checklist`,
`/team-release`, `/patch-notes`, `/changelog`

### Tooling and workflow maintenance

`/unreal-engine-mcp-codex`, `/skill-test`, `/skill-improve`, `/gate-check`

## Stop conditions

Stop and ask the user when:

- the requested behavior is materially unclear;
- required authorities conflict or are missing;
- prototype versus production mode is not explicitly selected;
- the task crosses the user's visual ownership boundary;
- the proposed write or validation scope expands;
- dirty work overlaps the proposed change and cannot be safely preserved;
- required evidence is unavailable;
- a destructive, external, asset, or Git action lacks authorization; or
- continuing requires a separate design, scheduling, implementation,
  publication, or acceptance decision.

If the work has become too large or context-heavy to complete reliably in the
current session, recommend `/new-session` with one bounded objective.

## Known workflow-system limitations

Check every generic skill against live Solarus authority before using it:

- Some skills still reference `.claude`, `CLAUDE.md`, generic `src/`, or
  non-Unreal asset paths.
- The main workflow catalog does not list every installed skill.
- Some catalog artifact checks do not match this Unreal repository layout.
- Generic prototype rules do not always match the Solarus prototype boundary.
- Team skills may attempt visual decisions owned by the user.
- Git-oriented skills cannot override the prohibition on unauthorized Git
  operations.

Do not treat a generic skill's assumptions as current project facts. Live
source, current roadmap packages, repository instructions, dirty work, and
fresh exported evidence take precedence.

## Completion rule for workflow advice

Workflow guidance is complete when the user knows:

1. the verified current situation;
2. exactly one recommended next action;
3. what is not yet authorized;
4. what follows after that action; and
5. which facts remain unknown.
