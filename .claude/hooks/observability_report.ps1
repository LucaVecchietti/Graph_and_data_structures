# Aggregate .claude/observability/events.jsonl into a metrics summary (JSON).
#
# Usage:
#   powershell -NoProfile -File .claude/hooks/observability_report.ps1
#   powershell -NoProfile -File .claude/hooks/observability_report.ps1 -Save
#   powershell -NoProfile -File .claude/hooks/observability_report.ps1 -Session <id>
#
# Prints the summary JSON to stdout; -Save also writes .claude/observability/metrics.json.

param(
    [string]$LogFile = (Join-Path (Split-Path $PSScriptRoot -Parent) "observability\events.jsonl"),
    [string]$Session,
    [switch]$Save
)

if (-not (Test-Path -LiteralPath $LogFile)) { Write-Output "{}"; exit 0 }

$events = @()
foreach ($l in Get-Content -LiteralPath $LogFile) {
    if ($l.Trim()) { try { $events += ($l | ConvertFrom-Json) } catch {} }
}
if ($Session) { $events = @($events | Where-Object { $_.session -eq $Session }) }

$tools = @($events | Where-Object { $_.event -eq 'PostToolUse' -and $_.tool })

$byTool = [ordered]@{}
$tools | Group-Object tool | Sort-Object Count -Descending | ForEach-Object { $byTool[$_.Name] = $_.Count }

$skills = [ordered]@{}
@($tools | Where-Object { $_.tool -eq 'Skill' }) | Group-Object target | Sort-Object Count -Descending | ForEach-Object { $skills[$_.Name] = $_.Count }

$agents = [ordered]@{}
@($tools | Where-Object { $_.tool -eq 'Task' }) | Group-Object target | Sort-Object Count -Descending | ForEach-Object { $agents[$_.Name] = $_.Count }

$fileTools = 'Read', 'Edit', 'Write', 'NotebookEdit', 'Grep', 'Glob'
$files = [ordered]@{}
@($tools | Where-Object { $fileTools -contains $_.tool -and $_.target }) | Group-Object target | Sort-Object Count -Descending | ForEach-Object { $files[$_.Name] = $_.Count }

$turns    = @($events | Where-Object { $_.event -eq 'UserPromptSubmit' }).Count
$sessions = @($events | Select-Object -ExpandProperty session -Unique).Count

# Duration aggregation (PostToolUse records that carry dur_ms).
$withDur = @($tools | Where-Object { $null -ne $_.dur_ms })
$durByTool = [ordered]@{}
$withDur | Group-Object tool | Sort-Object { ($_.Group | Measure-Object dur_ms -Sum).Sum } -Descending | ForEach-Object {
    $vals = @($_.Group | ForEach-Object { [double]$_.dur_ms })
    $sum  = ($vals | Measure-Object -Sum).Sum
    $durByTool[$_.Name] = [ordered]@{
        calls    = $_.Count
        total_ms = [long]$sum
        avg_ms   = [long]($sum / $_.Count)
        max_ms   = [long](($vals | Measure-Object -Maximum).Maximum)
    }
}
$totalMs = if ($withDur.Count) { [long](($withDur | ForEach-Object { [double]$_.dur_ms } | Measure-Object -Sum).Sum) } else { 0 }

$summary = [ordered]@{
    generated_at = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
    log_file     = $LogFile
    scope        = if ($Session) { "session:$Session" } else { "all" }
    totals       = [ordered]@{
        events         = $events.Count
        sessions       = $sessions
        turns          = $turns
        tool_calls     = $tools.Count
        tool_time_ms   = $totalMs
    }
    tools_used       = $byTool
    skills_used      = $skills
    agents_used      = $agents
    files_referenced = $files
    durations        = [ordered]@{
        measured_calls = $withDur.Count
        total_ms       = $totalMs
        by_tool        = $durByTool
    }
}

$json = $summary | ConvertTo-Json -Depth 6
Write-Output $json

if ($Save) {
    $out = Join-Path (Split-Path $LogFile -Parent) "metrics.json"
    [System.IO.File]::WriteAllText($out, $json, (New-Object System.Text.UTF8Encoding($false)))
}
