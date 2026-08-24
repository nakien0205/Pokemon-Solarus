param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet(
        "session-start",
        "pre-tool",
        "post-tool",
        "pre-compact",
        "post-compact",
        "subagent-start",
        "subagent-stop",
        "session-end"
    )]
    [string]$Mode
)

$ErrorActionPreference = "Stop"
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new($false)
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding

function Read-HookInput {
    $raw = [Console]::In.ReadToEnd()
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return $null
    }

    try {
        return $raw | ConvertFrom-Json
    }
    catch {
        return $null
    }
}

function Write-HookJson([object]$Value) {
    $Value | ConvertTo-Json -Depth 8 -Compress | Write-Output
}

function Get-RepositoryRoot([object]$HookInput) {
    $candidate = if ($null -ne $HookInput -and -not [string]::IsNullOrWhiteSpace([string]$HookInput.cwd)) {
        [System.IO.Path]::GetFullPath([string]$HookInput.cwd)
    }
    else {
        [System.IO.Path]::GetFullPath((Get-Location).Path)
    }

    $gitRoot = (& git -C $candidate rev-parse --show-toplevel 2>$null)
    if (-not [string]::IsNullOrWhiteSpace($gitRoot)) {
        return [System.IO.Path]::GetFullPath($gitRoot.Trim())
    }

    return $candidate
}

function Ensure-LogDirectory([string]$RepositoryRoot) {
    $directory = Join-Path $RepositoryRoot "production/session-logs"
    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }
    return $directory
}

function Get-EditedPaths([object]$HookInput) {
    $paths = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)

    if ($null -ne $HookInput -and $null -ne $HookInput.tool_input) {
        $filePath = [string]$HookInput.tool_input.file_path
        if (-not [string]::IsNullOrWhiteSpace($filePath)) {
            [void]$paths.Add($filePath)
        }

        $patch = [string]$HookInput.tool_input.command
        if (-not [string]::IsNullOrWhiteSpace($patch)) {
            foreach ($match in [regex]::Matches(
                $patch,
                '(?m)^\*\*\* (?:Add|Update) File:\s*(.+?)\s*$')) {
                [void]$paths.Add($match.Groups[1].Value)
            }
        }
    }

    return @($paths)
}

$hookInput = Read-HookInput
$repositoryRoot = Get-RepositoryRoot $hookInput
Set-Location -LiteralPath $repositoryRoot

switch ($Mode) {
    "session-start" {
        $branch = (& git rev-parse --abbrev-ref HEAD 2>$null)
        Write-Output "=== Pokemon Solarus Codex Context ==="
        if (-not [string]::IsNullOrWhiteSpace($branch)) {
            Write-Output "Branch: $branch"
        }

        $statePath = Join-Path $repositoryRoot "production/session-state/active.md"
        if (Test-Path -LiteralPath $statePath) {
            $lineCount = @(Get-Content -LiteralPath $statePath -Encoding UTF8).Count
            Write-Output "Active state: production/session-state/active.md ($lineCount lines). Read it before continuing ongoing work."
            Write-Output "Preview:"
            Get-Content -LiteralPath $statePath -Encoding UTF8 -TotalCount 18
        }
        else {
            Write-Output "WARNING: production/session-state/active.md is missing."
        }

        Write-Output "Preserve unrelated dirty work and do not commit without explicit user instruction."
        Write-Output "======================================="
    }

    "pre-tool" {
        $command = [string]$hookInput.tool_input.command
        if ([string]::IsNullOrWhiteSpace($command)) {
            break
        }

        if ($command -match '^\s*git\s+commit(?:\s|$)') {
            $stagedJson = @(& git diff --cached --name-only --diff-filter=ACMR 2>$null) |
                Where-Object { $_ -match '^(assets/data|Game/SourceData)/.*\.json$' }

            foreach ($relativePath in $stagedJson) {
                $fullPath = Join-Path $repositoryRoot $relativePath
                try {
                    Get-Content -LiteralPath $fullPath -Encoding UTF8 -Raw | ConvertFrom-Json | Out-Null
                }
                catch {
                    [Console]::Error.WriteLine("BLOCKED: $relativePath is not valid JSON: $($_.Exception.Message)")
                    exit 2
                }
            }
        }

        if ($command -match '^\s*git\s+push(?:\s|$)') {
            $branch = (& git rev-parse --abbrev-ref HEAD 2>$null)
            if ($branch -in @("main", "master", "develop")) {
                Write-HookJson @{
                    systemMessage = "Push to protected branch '$branch' detected. Confirm the requested validation evidence before proceeding."
                }
            }
        }
    }

    "post-tool" {
        $messages = [System.Collections.Generic.List[string]]::new()
        foreach ($editedPath in (Get-EditedPaths $hookInput)) {
            $normalized = $editedPath.Replace('\', '/')
            if ($normalized -match '(^|/)\.codex/skills/([^/]+)/') {
                $messages.Add("Skill '$($Matches[2])' changed; run its static skill validation before acceptance.")
            }

            if ($normalized -match '(^|/)(assets/data|Game/SourceData)/.*\.json$') {
                $fullPath = if ([System.IO.Path]::IsPathRooted($editedPath)) {
                    [System.IO.Path]::GetFullPath($editedPath)
                }
                else {
                    [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $editedPath))
                }

                if ($fullPath.StartsWith($repositoryRoot, [System.StringComparison]::OrdinalIgnoreCase) -and
                    (Test-Path -LiteralPath $fullPath)) {
                    try {
                        Get-Content -LiteralPath $fullPath -Encoding UTF8 -Raw | ConvertFrom-Json | Out-Null
                    }
                    catch {
                        $messages.Add("JSON validation failed for '$normalized': $($_.Exception.Message)")
                    }
                }
            }
        }

        if ($messages.Count -gt 0) {
            Write-HookJson @{
                systemMessage = ($messages -join "`n")
            }
        }
    }

    "pre-compact" {
        $logDirectory = Ensure-LogDirectory $repositoryRoot
        $trigger = if ($null -ne $hookInput) { [string]$hookInput.trigger } else { "unknown" }
        $message = "Context compaction ($trigger) at $(Get-Date -Format o)."
        Add-Content -LiteralPath (Join-Path $logDirectory "compaction-log.txt") -Value $message
    }

    "post-compact" {
        Write-HookJson @{
            hookSpecificOutput = @{
                hookEventName = "PostCompact"
                additionalContext = "Read production/session-state/active.md and the live files it names before continuing. Preserve unrelated dirty work."
            }
        }
    }

    "subagent-start" {
        $logDirectory = Ensure-LogDirectory $repositoryRoot
        $agentType = if ($null -ne $hookInput) { [string]$hookInput.agent_type } else { "unknown" }
        $agentId = if ($null -ne $hookInput) { [string]$hookInput.agent_id } else { "unknown" }
        $message = "$(Get-Date -Format o) | Agent started: $agentType ($agentId)"
        Add-Content -LiteralPath (Join-Path $logDirectory "agent-audit.log") -Value $message
    }

    "subagent-stop" {
        $logDirectory = Ensure-LogDirectory $repositoryRoot
        $agentType = if ($null -ne $hookInput) { [string]$hookInput.agent_type } else { "unknown" }
        $agentId = if ($null -ne $hookInput) { [string]$hookInput.agent_id } else { "unknown" }
        $message = "$(Get-Date -Format o) | Agent stopped: $agentType ($agentId)"
        Add-Content -LiteralPath (Join-Path $logDirectory "agent-audit.log") -Value $message
    }

    "session-end" {
        $logDirectory = Ensure-LogDirectory $repositoryRoot
        $statePath = Join-Path $repositoryRoot "production/session-state/active.md"
        $sessionLog = Join-Path $logDirectory "session-log.md"
        $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"

        Add-Content -LiteralPath $sessionLog -Value "## Session End: $timestamp"
        if (Test-Path -LiteralPath $statePath) {
            Add-Content -LiteralPath $sessionLog -Value "### Active State"
            Get-Content -LiteralPath $statePath -Encoding UTF8 | Add-Content -LiteralPath $sessionLog
        }

        $changed = @(& git status --short 2>$null)
        if ($changed.Count -gt 0) {
            Add-Content -LiteralPath $sessionLog -Value "### Working Tree"
            $changed | Add-Content -LiteralPath $sessionLog
        }
        Add-Content -LiteralPath $sessionLog -Value "---`n"
    }
}
