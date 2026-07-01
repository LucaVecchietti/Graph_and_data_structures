# Stop hook: nudge Claude to keep docs/ in sync (pairs with the docs-keeper skill).
#
# Fires only when there are uncommitted changes to DOC-RELEVANT source
# (graph_core/ or data_tructures/) while nothing under docs/ was touched this
# session. Loop-safe: honors `stop_hook_active` so it blocks at most once.

$ErrorActionPreference = 'SilentlyContinue'

$raw = [Console]::In.ReadToEnd()
try { $data = $raw | ConvertFrom-Json } catch { exit 0 }

# Prevent infinite loops: if we already blocked once, let the stop proceed.
if ($data.stop_hook_active) { exit 0 }

$root = (git rev-parse --show-toplevel).Trim()
if (-not $root) { exit 0 }
$status = git -C $root status --porcelain
if (-not $status) { exit 0 }   # nothing changed -> nothing to document

$changed = @()
foreach ($line in ($status -split "`n")) {
    if ($line.Trim().Length -gt 0) { $changed += $line.Substring(3).Trim() }
}

# Doc-relevant source: struct/policies/io/odt layer and core data structures.
$code = $changed | Where-Object {
    ($_ -like "graph_core/*") -or ($_ -like "data_tructures/*")
}
$docs = $changed | Where-Object { $_ -like "docs/*" }

# Only nudge when doc-relevant code changed but docs/ was left untouched.
if ($code -and -not $docs) {
    $reason = "Docs sync check: there are uncommitted changes under graph_core/ or " +
        "data_tructures/ but nothing under docs/ was updated this session. If any " +
        "change altered the on-disk format, a POD/domain struct, a policy, the io/ or " +
        "odt/ layer, the Graph public surface, or added a module, invoke the " +
        "``docs-keeper`` skill to update the affected docs (and their metadata table: " +
        "Ultimo aggiornamento + Commit di riferimento), plus the relevant legacy log " +
        "(design_decisions / api_changes / known_bugs). If the change is trivial or " +
        "internal-only with no doc impact, you may stop now."
    $out = @{ decision = "block"; reason = $reason } | ConvertTo-Json -Compress
    Write-Output $out
}

exit 0
