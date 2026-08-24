# Active Codex Hooks

Hooks are configured in `.codex/hooks.json`. Windows execution uses the native
PowerShell runner `.codex/hooks/project-hooks.ps1`; it does not depend on WSL or
Git Bash.

| Event | Trigger | Action |
| ----- | ------- | ------ |
| `SessionStart` | Start, resume, clear, or compact recovery | Reports the branch and previews `production/session-state/active.md` as developer context |
| `PreToolUse` | Shell command | Validates staged Battle source JSON before a commit and warns before a protected-branch push |
| `PostToolUse` | `apply_patch` / Write / Edit | Validates edited JSON and reminds Codex to validate changed skills |
| `PreCompact` | Manual or automatic compaction | Records the compaction event under ignored `production/session-logs/` |
| `PostCompact` | Manual or automatic compaction | Injects a reminder to reload the active state and live files |
| `SubagentStart` / `SubagentStop` | Subagent lifecycle | Appends the agent type and ID to the ignored audit log |
| `SessionEnd` | Main session closes | Archives the active state and working-tree summary to the ignored session log |

Codex does not expose the old Claude Code `Notification` hook event. Desktop
notifications are therefore left to Codex application settings rather than a
repository hook.

Hook schemas: `.codex/docs/hooks-reference/hook-input-schemas.md`.
