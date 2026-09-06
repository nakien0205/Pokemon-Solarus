# Pokemon Solarus Workflow Guide

## Purpose

This file routes project work without turning every change into a production
process.

It does not authorize edits, assets, Git operations, or future roadmap work.

The governing rule is:

> **The final vision decides where Solarus is going. The current playable
> milestone decides what Solarus builds now.**

Future requirements may shape a clean extension point. They do not create
current work.

---

## Development Modes

| Mode           | Purpose                                                  | Default rigor                                  |
| -------------- | -------------------------------------------------------- | ---------------------------------------------- |
| **SPIKE**      | Answer one uncertain question                            | Fast, disposable, minimal validation           |
| **PLAYABLE**   | Deliver the next player-visible result                   | Small implementation, focused tests, real PIE  |
| **PRODUCTION** | Harden proven systems or prepare production/release work | Formal approval, broader regression and review |

`production/session-state/active.md` records the current mode.

If no mode is recorded, use **PLAYABLE**.

Do not choose Production simply because the task could theoretically benefit
from more rigor.

---

## Authority Order

Use the smallest relevant authority set.

1. Higher-priority instructions and the user's current request.
2. `AGENTS.md`.
3. Current user-named accepted design/ADR/task authority.
4. `production/session-state/active.md`.
5. Live source, tests, runtime state, and fresh evidence.
6. Current roadmap/index documents.
7. Historical documents and reports.

When an accepted current authority explicitly requires a stronger process, such
as an ADR, follow that requirement. Keep it bounded to the current milestone.

Do not allow historical requirements to create new work unless the active task
depends on them.

---

## Context Rule

Do not begin by reading the entire repository.

Start with:

`AGENTS.md -> active.md -> docs/index.md -> current authority -> relevant code/tests`

Search before opening broad directories.

Follow links only when they can materially affect the current task.

Historical reports, old plans, evidence roots, unrelated GDDs, and unrelated
Battle subsystems are not default reading.

---

# Default PLAYABLE Workflow

Most Solarus development should currently use this path:

**Goal -> Decide only what is unclear -> Implement -> Focused Validation -> PIE -> Accept/Fix -> Stop**

## 1. Goal

Define one player-visible or directly enabling outcome.

Examples:

* the player can choose one of the currently assigned moves;
* the Bag child flow opens and safely returns;
* one Potion can restore HP;
* two Pokemon can be switched;
* a move's result becomes visible in battle.

Do not turn one goal into an entire future subsystem.

## 2. Decide Only What Is Unclear

If an accepted design already settles the behavior, use it.

Do not run another design workflow to reconsider settled decisions.

If one small behavior remains unclear, resolve only that behavior through a
small decision or `/quick-design`.

Use `/design-system` only when a genuinely new major system lacks an accepted
design.

## 3. Implement

Before mutation, identify the small expected write set and exclusions.

If the user directly requested implementation of an approved bounded task, that
request authorizes implementation of that scope.

Do not require a separate Solarus implementation draft and independent approval
for normal PLAYABLE work.

Do not expand the write set to unrelated cleanup, future-proofing, catalog
closure, refactoring, or architecture improvement.

If the implementation proves that a larger boundary must change, stop the
expansion and identify the exact reason.

## 4. Focused Validation

Validate what changed.

Typical PLAYABLE validation is:

* relevant focused automated tests;
* one normal Editor build when C++ changed;
* Blueprint/widget compilation when relevant;
* actual PIE testing for player-facing behavior.

Do not automatically run the complete Battle suite.

## 5. PIE

A player-facing feature is not complete merely because native tests pass.

Verify the intended interaction in Unreal.

The user owns visual judgment.

## 6. Accept or Fix

Fix concrete defects found by validation or PIE.

Once the approved outcome works, stop.

Do not continue into cleanup, polish, generalization, or the next roadmap step
without a new task.

---

# SPIKE Workflow

Use a spike when one uncertain question blocks a decision.

Path:

**Question -> Small experiment -> Observe -> Decide -> Discard or promote**

A spike should:

* answer one question;
* use the smallest test environment possible;
* avoid production architecture;
* avoid broad test suites;
* avoid permanent documentation unless the result changes a real project
  decision.

A successful spike is not automatically production code.

---

# PRODUCTION Workflow

Use Production mode when the user explicitly chooses it, when preparing
production/release work, or when a current accepted authority genuinely
requires production-level guarantees.

A Production task may use:

**Design -> Implementation Draft -> Independent Approval -> Implementation ->
Broader Validation -> Code Review -> Test-Evidence Review -> Acceptance**

Use this process selectively.

It is not the default path for ordinary gameplay development.

Production rigor is appropriate for examples such as:

* changing persistent save formats;
* changing fundamental state ownership;
* changing a widely consumed public/reflected API;
* replay-schema or deterministic-RNG migration;
* release gates;
* risky data migration;
* high-impact shared architecture already proven necessary.

---

# Architecture Decisions

Use `/architecture-decision` only when:

1. the current accepted design explicitly requires an ADR; or
2. the current task changes a fundamental architecture boundary.

Examples of real architecture triggers:

* authoritative state ownership;
* persistence;
* module boundaries;
* broadly consumed public/reflected contracts;
* transaction ownership;
* RNG ownership;
* replay/event publication;
* lifecycle/atomicity guarantees that materially affect current correctness.

An ADR must solve the current problem.

It must not design future roadmap systems merely because they may eventually
touch the same boundary.

Do **not** require an ADR for ordinary:

* UI code-behind;
* adding authored content;
* wiring an already-supported decision;
* local bug fixes;
* move-selector behavior;
* isolated presentation data;
* mechanical refactoring that preserves ownership and contracts.

Unless a current authority explicitly says otherwise.

---

# Review Rules

Reviews exist to catch real problems, not generate infinite work.

Classify findings as:

### BLOCKER

A finding may block when it demonstrates:

* incorrect current behavior;
* violation of an accepted current requirement;
* data corruption/loss risk;
* security or serious stability risk;
* an architecture defect preventing the current or immediately next milestone.

### DEBT / ADVISORY

Examples:

* future scaling concerns;
* future extensibility opportunities;
* cleaner abstractions not currently needed;
* speculative interactions with later roadmap systems;
* optional refactoring;
* additional test coverage beyond the current risk.

These must not block PLAYABLE work.

Record useful findings as technical debt and continue.

## Review Loop Limit

Default:

**Review -> one correction pass -> one confirmation**

Do not start another review/fix cycle unless a concrete unresolved BLOCKER
remains.

A new reviewer discovering a different speculative issue is not, by itself, a
reason to restart the cycle.

---

# Validation Ladder

## Local PLAYABLE Change

Use:

**focused tests -> relevant build -> PIE if player-facing**

## Shared-Core Change

If BattleEngine or another widely shared owner changes:

**new focused tests -> affected older filters -> relevant build -> PIE**

Run only the regressions that can reasonably be affected.

## Playable Milestone Completion

At milestone completion:

* run the milestone's acceptance validation;
* run broader Battle regression if shared systems changed materially;
* perform one broader code/architecture review if useful;
* perform a complete manual playable pass.

## PRODUCTION / Release

Use whatever full evidence, suite, compatibility proof, review, and publication
gates the production task explicitly requires.

Do not use release-level validation for every feature.

---

# Documentation Rule

Documentation should reduce future reading, not create more required reading.

For ordinary PLAYABLE work, update only:

* the directly affected accepted design if its approved behavior changed;
* `production/session-state/active.md`;
* a necessary architecture authority if a real architecture decision changed.

Do not create duplicate:

* status documents;
* implementation summaries;
* acceptance reports;
* evidence narratives;
* handoff documents;

unless they solve an actual continuity problem or Production mode requires them.

Generated test/build evidence may exist without becoming mandatory future
reading.

`active.md` is a current-state pointer, not a historical archive.

---

# Battle Workflow

For current Battle development:

1. Read the active milestone.
2. Read its accepted design.
3. Use `index.md` to locate only the relevant Battle files.
4. Treat the existing BattleEngine and completed mechanics as a stable
   foundation.
5. Change shared Battle core only when the active milestone proves a concrete
   gap.
6. Preserve generic runtime behavior; do not add species-, move-, or
   showcase-specific branches.
7. Validate the changed path.
8. Prove the result in PIE.
9. Stop when the active milestone is playable.

Do not automatically:

* close C11A deferred catalog gaps;
* revisit completed C10/C11 work;
* improve replay;
* improve transactional semantics;
* refactor the BattleEngine;
* add future effects;
* add strategic AI;
* implement later roadmap steps;

unless the current task explicitly requires one of them.

---

# Compact Workflow Router

| Need                                   | Use                                             |
| -------------------------------------- | ----------------------------------------------- |
| Approved playable feature              | Default PLAYABLE path                           |
| One small unresolved behavior          | `/quick-design`                                 |
| Large undesigned system                | `/design-system`                                |
| Genuine architecture trigger           | `/architecture-decision`                        |
| Reproducible bug                       | `/bug-report` then minimal PLAYABLE fix         |
| Live Unreal asset/Blueprint inspection | `/unreal-engine-mcp-codex`                      |
| Major milestone review                 | One bounded review after the playable milestone |
| Production/release hardening           | PRODUCTION path                                 |
| Fresh-session continuity               | `/handoff`                                      |

Do not chain skills automatically.

Select only the workflow needed for the unresolved phase.

---

# User-Owned Presentation

The user owns UI/UX appearance, art, layout, styling, motion appearance, audio
treatment, and visual references.

Do not route ordinary implementation through art/UX review gates.

Functional presentation contracts may still be implemented and validated:

* state;
* input;
* bindings;
* data;
* adapters;
* error handling;
* code-behind;
* Blueprint plumbing.

Visual approval comes from the user.

---

# Content and Data

Add only the content needed by the active playable milestone.

Do not create a complete content database because later systems will need one.

For third-party assets, follow `docs/registry/external-assets.yaml`.

A later roadmap requirement may be recorded without implementing its data now.

---

# Bugs and Maintenance

For a normal bug:

**Reproduce -> identify cause -> smallest fix -> focused regression -> PIE if
relevant -> stop**

Do not use a bug as an excuse for unrelated cleanup.

Technical debt is scheduled separately unless it blocks the active milestone.

---

# Performance

Do not optimize based on speculation.

Use:

**measure -> identify a real bottleneck -> smallest fix -> remeasure**

Future scale is not a current performance bug.

---

# Git

Implementation authorization is not Git authorization.

Do not stage, commit, push, branch, reset, checkout, or alter history unless the
user explicitly requests that Git operation.

---

# Stop / Escalation Conditions

Pause expansion of the task when:

* the requested implementation would exceed the active milestone;
* a current accepted authority genuinely conflicts with another;
* a real architecture trigger appears that was not approved;
* the change crosses the user's visual ownership boundary;
* unrelated dirty work would be overwritten;
* a required external/destructive/Git action lacks authorization.

When the issue is only a speculative future concern, do **not** stop the current
milestone. Record it as advisory technical debt.

---

# Completion Rule

A PLAYABLE task is complete when:

1. the approved current outcome works;
2. relevant focused tests pass;
3. the necessary build succeeds;
4. required PIE behavior is verified;
5. no concrete current blocker remains.

Then stop.

The next roadmap step requires a new task.
