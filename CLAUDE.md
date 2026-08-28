# Claude Code Game Studios -- Game Studio Agent Architecture

Indie game development managed through 49 coordinated Claude Code subagents.
Each agent owns a specific domain, enforcing separation of concerns and quality.

## Technology Stack

- **Engine**: Unreal Engine 5.8.1 (changelist 56057345)
- **Language**: C++ (primary), Blueprint (gameplay prototyping)
- **Version Control**: Not initialized; do not initialize Git or commit without user instruction
- **Build System**: Unreal Build Tool (UBT)
- **Asset Pipeline**: Unreal Content Pipeline

> **Note**: Engine-specialist agents exist for Godot, Unity, and Unreal with
> dedicated sub-specialists. Use the set matching your engine.

## Project Structure

@.codex/docs/directory-structure.md

## Engine Version Reference

@docs/engine-reference/unreal/VERSION.md

## Technical Preferences

@.codex/docs/technical-preferences.md

## Coordination Rules

@.codex/docs/coordination-rules.md

## Collaboration Protocol

**User-driven collaboration, not autonomous execution.**
Every task follows: **Question -> Options -> Decision -> Draft -> Approval**

- Agents MUST ask "May I write this to [filepath]?" before using Write/Edit tools
- Agents MUST show drafts or summaries before requesting approval
- Multi-file changes require explicit approval for the full changeset
- No commits without user instruction

See `docs/COLLABORATIVE-DESIGN-PRINCIPLE.md` for full protocol and examples.

> **First session?** If the project has no engine configured and no game concept,
> run `/start` to begin the guided onboarding flow.

## Coding Standards

@.codex/docs/coding-standards.md

## Mandatory Code File Organization

Before planning, creating, or modifying hand-authored code or automated tests,
read and follow this project-wide guide:

@docs/code-file-organization.md

## Context Management

@.codex/docs/context-management.md

## User preference

1. The user own frontend and UI/UX visual design and visual assets, including layout, styling, art, materials, textures, widget or scene composition, and the appearance of motion. Codex owns backend mechanics and code-behind, including state machines, input routing, data contracts, adapters, validation, and automated tests. Codex may expose bindings and events or inspect and review frontend work, but must not create or modify frontend visual decisions or assets unless I explicitly grant a task-specific exception. A broad implementation request does not override this rule.
2. After completing a task, you must tell me what is the next step and do not leave me hanging, guessing what the next step is.
