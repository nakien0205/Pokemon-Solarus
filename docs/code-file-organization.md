# Code File Organization Guide

## Status and scope

**This guide is mandatory and project-wide. Read it before planning, creating,
or modifying hand-authored code or automated tests.**

It applies to production code, tests, tools, and scripts in this repository. It
does not authorize changes to generated Unreal output, third-party code, or
unrelated existing files. Existing large files are not automatically part of a
new task.

## Purpose

Avoid monolithic files by giving each file one clear responsibility. Smaller
files should make ownership, review, testing, and future changes easier without
breaking behavior or hiding dependencies.

File length is a warning signal, not the design rule. A cohesive atomic workflow
may be safer in one file than split across arbitrary boundaries. Split when a
file owns independent responsibilities, not merely to reach a line-count target.

## Required organization rule

Every hand-authored code file must own one coherent responsibility, workflow,
or closely related behavior family.

- Keep public facades and coordinators focused. They should expose the contract
  and sequence work rather than accumulate every implementation detail.
- Put a new independent responsibility in a focused file instead of adding it
  to an existing catch-all file.
- Name files after the behavior or responsibility they own. Avoid vague files
  such as `Misc`, `Common`, or `Utils` unless their exact shared purpose is
  narrow and documented.
- Keep one authoritative owner for state and transactions. Splitting source
  files must not create a second state owner, random-number owner, commit seam,
  or publication path.
- Do not create a new Unreal module solely to reduce file length.

## Plan the file map before implementation

For a meaningful code change, the implementation draft must state:

1. The files that will be created or modified.
2. The single responsibility owned by each file.
3. Which helpers remain file-local and which are genuinely shared.
4. The public APIs, state ownership, transaction boundaries, event order, RNG
   behavior, test identities, and other live contracts that must remain stable.
5. The exact validation scope and the files that are excluded.

If the proposed change would give a file a second independent responsibility,
revise the file map before writing code or request an explicit exception.

## Keep cohesive work together

Do not split through a behavior merely to make files shorter.

- Move or implement a complete workflow, checkpoint, or behavior family
  together when its helpers and invariants are tightly coupled.
- Do not separate a branch embedded inside a larger public operation unless an
  approved internal seam already exists or the task explicitly creates and
  validates one.
- Do not introduce generic base classes, indirection, or public APIs only to
  make a file boundary easier.
- Keep mechanical relocation separate from behavior cleanup, deduplication, or
  feature work. Those changes need their own review and validation.

## Helper and dependency ownership

- If only one translation unit uses a helper, keep it local to that source
  file.
- If two or more focused translation units use a helper family, give it one
  explicit private declaration header and, when needed, one matching source
  file.
- Do not manually redeclare another source file's private helpers. Shared
  dependencies must have one visible declaration owner.
- Do not include a `.cpp` file from another `.cpp` file.
- Every source file must include what it needs and compile without relying on
  Unity-build include bleed.
- In Unreal C++, use a unique named private namespace for file-private symbols,
  such as `BattleEngineActionStartPrivate`. Repeated anonymous or file-static
  names can collide when Unity builds combine sources.
- Keep private declarations private. Do not expand a public header merely to
  share implementation details inside one module.

## Project line-count guardrails

These are review triggers, not automatic split points and not measures of code
quality. Count physical lines, including comments and blank lines, consistently.

- At 500 lines, check whether the changed file now owns more than one
  responsibility.
- At 1,000 lines, the implementation draft must record an explicit organization
  decision: either a focused split map or the reason the file remains one
  cohesive unit.
- Above 2,000 lines, a changed file must not gain another independent
  responsibility without splitting that responsibility into a focused file or
  receiving a task-specific approved exception.

A focused bug fix or change to an existing cohesive responsibility does not
automatically authorize a broad legacy split. Conversely, being below a
threshold does not justify mixing unrelated responsibilities.

## Tests follow behavior boundaries

- Organize tests by behavior family, package, or contract rather than collecting
  unrelated tests in one growing file.
- Keep family-specific fixtures and helpers local. Promote only helpers used by
  multiple test families into focused private support files.
- During a structural test split, prove that every old test identity exists
  exactly once in the new layout before removing the old file.
- A test relocation must not silently rename, omit, duplicate, disable, or
  weaken a test.

## Safe structural split process

When an existing file must be split:

1. Inspect live source, contracts, callers, tests, active guides, and dirty
   paths before proposing the boundary.
2. Record the current responsibility map and the intended focused file map.
3. Use small, separately approved waves when dependency or behavior risk is
   high.
4. Move complete definitions and helper families without changing their logic.
5. Prove every moved definition exists exactly once and no obsolete duplicate
   remains.
6. Update every active guide that names the old file, line location, or moved
   responsibility. Preserve dated evidence as historical evidence.
7. Run validation appropriate to the affected shared surface, then review the
   final diff before acceptance.

For Unreal C++ structural work, validation must include an independent
translation-unit or forced-Unity build when required by the task. Run the
approved affected Automation filters after shared engine or executor changes.
Judge Automation using exported `index.json` counters and test entries, not the
process exit code alone.

## Completion checklist

A new layout or structural split is complete only when:

- every file has one stated responsibility;
- every definition and test identity exists exactly once;
- public contracts, state ownership, atomic boundaries, event order, RNG use,
  and publication behavior remain correct;
- no `.cpp` includes another `.cpp`;
- each source has self-contained includes and Unity-safe private names;
- approved builds and tests pass with their required evidence;
- active guides describe the live structure accurately;
- unrelated and generated files were not hand-edited; and
- the user reviewed and approved the final diff and evidence.

## Exceptions

An exception must be task-specific and approved before implementation. State:

- why keeping the code together is safer or clearer;
- why the normal split boundary is unsuitable;
- which risks and validation apply; and
- what future condition should trigger another organization review.

An exception to a line-count guardrail is not permission to mix unrelated
responsibilities.

## Project example

The completed Battle structural migration is the repository's worked example:
[`battle-engine-structural-split-handoff.md`](battle-engine-structural-split-handoff.md).
It demonstrates responsibility-based translation units, complete checkpoint
families, private helper ownership, behavior-preserving waves, guide migration,
forced-Unity validation, and exact test-path verification. Its Battle-specific
file map and test matrix are historical task evidence, not universal values for
every future split.
