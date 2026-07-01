# Stop hook: nudge Claude to sync the knowledge graph.
#
# Fires only when there are uncommitted changes OUTSIDE .claude/knowledge/
# while the graph itself was NOT touched this session. Loop-safe: honors
# `stop_hook_active` so it blocks at most once per stop.

$ErrorActionPreference = 'SilentlyContinue'

$raw = [Console]::In.ReadToEnd()
try { $data = $raw | ConvertFrom-Json } catch { exit 0 }

# Prevent infinite loops: if we already blocked once, let the stop proceed.
if ($data.stop_hook_active) { exit 0 }

$root = (git rev-parse --show-toplevel).Trim()
if (-not $root) { exit 0 }
$status = git -C $root status --porcelain
if (-not $status) { exit 0 }   # nothing changed -> nothing to capture

$changed = @()
foreach ($line in ($status -split "`n")) {
    if ($line.Trim().Length -gt 0) { $changed += $line.Substring(3).Trim() }
}

$kg   = $changed | Where-Object { $_ -like ".claude/knowledge/*" }
# "Real project" changes: any tracked change OUTSIDE .claude/ (source, docs,
# build config, main.cpp, README, ...). Excluding all of .claude/ keeps meta-work
# on skills/hooks/settings/observability from triggering a graph-sync nudge.
$code = $changed | Where-Object { $_ -notlike ".claude/*" }

# Only nudge when real project changes exist but the graph was left untouched.
if ($code -and -not $kg) {
    $reason = "Knowledge-graph sync check: there are uncommitted changes outside " +
        ".claude/knowledge/ but the knowledge graph was not updated this session. " +
        "If any change introduced or altered project knowledge (architecture, modules, " +
        "on-disk format, decisions, workflows, invariants, bugs), invoke the " +
        "``knowledge-graph`` skill to update the affected nodes, write the inverse " +
        "[[wikilinks]], and register them in Index.md. If the changes are trivial " +
        "(formatting, typos, no conceptual impact), you may stop now."
    $out = @{ decision = "block"; reason = $reason } | ConvertTo-Json -Compress
    Write-Output $out
}

exit 0
