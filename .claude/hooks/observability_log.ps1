# Universal Claude Code event logger -> JSONL (agent-observability skill).
#
# Registered on PreToolUse, PostToolUse, UserPromptSubmit, SessionStart,
# SubagentStop, Stop. Reads the hook payload from stdin, normalizes a compact
# record, and appends one JSON line per event to .claude/observability/events.jsonl.
#
# Durations: PreToolUse stores a start timestamp in .pending.json keyed by
# tool_use_id (with a fallback hash); PostToolUse pairs on that key and emits
# `dur_ms`. PreToolUse itself writes NO events line (keeps one line per tool call).
#
# MUST stay silent on stdout (a stray print on Pre/UserPromptSubmit would be
# injected as context) and MUST NOT block: it only logs, then exit 0.

$ErrorActionPreference = 'SilentlyContinue'

$raw = [Console]::In.ReadToEnd()
if (-not $raw) { exit 0 }
try { $e = $raw | ConvertFrom-Json } catch { exit 0 }

$event = $e.hook_event_name
$tool  = $e.tool_name
$ti    = $e.tool_input

# Normalize the "target" of the event per tool, so metrics can group on it.
$target = $null
$extra  = $null
switch ($tool) {
    { $_ -in 'Read', 'Edit', 'Write', 'NotebookEdit' } { $target = $ti.file_path }
    'Grep'       { $target = $ti.pattern; $extra = $ti.path }
    'Glob'       { $target = $ti.pattern; $extra = $ti.path }
    'Bash'       { $target = $ti.command }
    'PowerShell' { $target = $ti.command }
    'Task'       { $target = $ti.subagent_type; $extra = $ti.description }
    'Skill'      { $target = $ti.skill; $extra = $ti.args }
    'ToolSearch' { $target = $ti.query }
}
if ($target -is [string] -and $target.Length -gt 300) { $target = $target.Substring(0, 300) }
if ($extra  -is [string] -and $extra.Length  -gt 200) { $extra  = $extra.Substring(0, 200) }

$dir = Join-Path (Split-Path $PSScriptRoot -Parent) "observability"
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$logFile     = Join-Path $dir "events.jsonl"
$pendingFile = Join-Path $dir ".pending.json"
$enc = New-Object System.Text.UTF8Encoding($false)

# Correlation key to pair a PreToolUse with its PostToolUse.
$key = $e.tool_use_id
if (-not $key -and $tool) {
    $key = "$($e.session_id)|$tool|" + (($ti | ConvertTo-Json -Compress -Depth 10).GetHashCode())
}

$nowMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()

$mtx = New-Object System.Threading.Mutex($false, "claude_obs_log")
[void]$mtx.WaitOne(3000)
try {
    # Load pending start-times, prune stale (> 10 min, e.g. a tool that never returned).
    $pending = @{}
    if (Test-Path -LiteralPath $pendingFile) {
        try {
            $obj = Get-Content -LiteralPath $pendingFile -Raw | ConvertFrom-Json
            foreach ($p in $obj.PSObject.Properties) { $pending[$p.Name] = [long]$p.Value }
        } catch {}
    }
    foreach ($k in @($pending.Keys)) { if (($nowMs - $pending[$k]) -gt 600000) { $pending.Remove($k) } }

    if ($event -eq 'PreToolUse') {
        if ($key) { $pending[$key] = $nowMs }
        $pj = if ($pending.Count) { $pending | ConvertTo-Json -Compress } else { "{}" }
        [System.IO.File]::WriteAllText($pendingFile, $pj, $enc)
    }
    else {
        $dur = $null
        if ($event -eq 'PostToolUse' -and $key -and $pending.ContainsKey($key)) {
            $dur = $nowMs - $pending[$key]
            $pending.Remove($key)
            $pj = if ($pending.Count) { $pending | ConvertTo-Json -Compress } else { "{}" }
            [System.IO.File]::WriteAllText($pendingFile, $pj, $enc)
        }

        $rec = [ordered]@{
            ts      = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ss.fffZ")
            session = $e.session_id
            event   = $event
            tool    = $tool
            target  = $target
            extra   = $extra
        }
        if ($null -ne $dur) { $rec.dur_ms = $dur }
        if ($event -eq 'UserPromptSubmit') {
            $p = $e.prompt
            if ($p -is [string] -and $p.Length -gt 300) { $p = $p.Substring(0, 300) }
            $rec.prompt = $p
        }
        if ($event -eq 'SessionStart') { $rec.source = $e.source }
        if ($event -eq 'Notification') { $rec.message = $e.message }

        $line = ($rec | ConvertTo-Json -Compress -Depth 5)
        [System.IO.File]::AppendAllText($logFile, $line + "`n", $enc)
    }
}
finally { $mtx.ReleaseMutex() }

exit 0
