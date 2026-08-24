# Codex Hook Input/Output Notes

Every command hook receives JSON on standard input. Common fields include
`session_id`, `transcript_path`, `cwd`, `hook_event_name`, and `model`.

## Relevant event fields

| Event | Additional input |
| ----- | ---------------- |
| `SessionStart` | `source`: `startup`, `resume`, `clear`, or `compact` |
| `PreToolUse` / `PostToolUse` | `turn_id`, `tool_name`, `tool_use_id`, `tool_input`; Post also receives `tool_response` |
| `PreCompact` / `PostCompact` | `turn_id`, `trigger`: `manual` or `auto` |
| `SubagentStart` / `SubagentStop` | `turn_id`, `agent_id`, `agent_type` |
| `SessionEnd` | `reason` (currently `other`) |

Codex reports unified shell execution as `Bash`. File edits through
`apply_patch` report `tool_name: "apply_patch"`, but matchers may use
`apply_patch`, `Edit`, or `Write`. For `apply_patch`, the patch text is in
`tool_input.command`, so the repository runner extracts paths from patch headers.

## Output behavior used here

- Plain stdout from `SessionStart` becomes developer context.
- `PreToolUse` may block with exit code `2` and a reason on stderr.
- `PostToolUse` returns JSON `systemMessage` for validation warnings.
- `PostCompact` returns `hookSpecificOutput.additionalContext` so recovery
  guidance survives compaction.
- `SessionEnd` is advisory; its output does not steer Codex.

Official reference: `https://learn.chatgpt.com/docs/hooks`.
