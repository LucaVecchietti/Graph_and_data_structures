---
name: agent-observability
description: Use this skill to monitor and report on this agent's own effectiveness. A set of Claude Code hooks deterministically logs every tool call, subagent (Task), skill invocation, and file reference to .claude/observability/events.jsonl; this skill documents that log, tells Claude when to emit semantic "reflection" records, and how to aggregate everything into metrics. Activate when the user asks to "monitora l'agente", "osservabilità", "metriche", "genera un report", "quanto sto usando X", or invokes /agent-observability; and emit a reflection at the end of any substantive multi-step task.
---

# agent-observability

You maintain and use the **agent observability system** for this project: a deterministic event log plus a semantic reflection layer, so the user can measure how effective this agent is — which tools/skills/agents it uses, how often, and which context files inform its answers.

## Architecture

Two layers, by design:

1. **Deterministic backbone (hooks).** `.claude/hooks/observability_log.ps1` is registered on `PreToolUse`, `PostToolUse`, `UserPromptSubmit`, `SessionStart`, `SubagentStop`, and `Stop`. It fires automatically — no cooperation from Claude required — and appends one JSON line per event to `.claude/observability/events.jsonl`. This is the source of truth for *what happened*. `PreToolUse` records a start timestamp (in `.pending.json`) and writes no line of its own; the matching `PostToolUse` line carries the measured `dur_ms`.
2. **Semantic layer (this skill).** Hooks cannot know *which* context files actually informed an answer, or how well a task went. That judgment is yours: on substantive tasks you append a `reflection` record. This is the source of truth for *why / how well*.

Never try to hand-log routine tool calls — the hook already captures them, and self-logging pollutes context and is unreliable. Your only manual write is the reflection.

## Output files (`.claude/observability/`)

| File               | Written by            | Content                                              |
|--------------------|-----------------------|------------------------------------------------------|
| `events.jsonl`     | hook (auto)           | one event per line, append-only                      |
| `.pending.json`    | hook (auto)           | in-flight tool start times (for `dur_ms`); transient |
| `reflections.jsonl`| you (this skill)      | one reflection per substantive task                  |
| `metrics.json`     | report script (`-Save`)| aggregated summary snapshot                          |

The whole directory is gitignored (runtime data).

## Event record schema (`events.jsonl`)

```json
{"ts":"2026-07-01T10:11:12.345Z","session":"<id>","event":"PostToolUse","tool":"Read","target":"graph_core/graph.cpp","extra":null,"dur_ms":42}
```

- `event` — `PostToolUse` | `UserPromptSubmit` | `SessionStart` | `SubagentStop` | `Stop` (`PreToolUse` fires but writes no line — it only records the start time).
- `tool` — tool name on `PostToolUse` (null otherwise).
- `target` — normalized subject: file path (Read/Edit/Write/NotebookEdit), pattern (Grep/Glob), command (Bash/PowerShell), `subagent_type` (Task), skill name (Skill), query (ToolSearch).
- `extra` — secondary field (Grep/Glob `path`, Task `description`, Skill `args`).
- `dur_ms` — wall-clock duration of the tool call (Pre→Post), present on `PostToolUse` when the pairing succeeded.
- `UserPromptSubmit` records carry a truncated `prompt`; `SessionStart` carries `source`.

A **turn** is the span between a `UserPromptSubmit` and the next `Stop` (same `session`). Slice the log by that window to attribute events to a single response.

## Reflection record schema (`reflections.jsonl`)

Emit one at the **end of a substantive, multi-step task** (a refactor, an investigation, building something) — not for trivial one-shot answers. Keep it honest; this measures the agent, so do not inflate ratings.

```json
{
  "ts": "2026-07-01T10:20:00Z",
  "type": "reflection",
  "task": "one-line description of what was asked",
  "skills_used": ["knowledge-graph"],
  "agents_used": [],
  "context_files": ["CLAUDE.md", "graph_core/graph.cpp"],
  "tools_summary": {"Read": 4, "Edit": 2, "Bash": 3},
  "self_rating": {"effectiveness": 4, "confidence": 4, "notes": "what worked / what was uncertain"}
}
```

- `context_files` — the files that **actually informed** the answer (not every file touched). This is the piece hooks can't infer; it's the most valuable field.
- `self_rating.effectiveness` / `confidence` — 1–5, honest.

### How to append a reflection

Use the Bash tool (portable, UTF-8, single append). Build the JSON as one line:

```bash
printf '%s\n' '{"ts":"2026-07-01T10:20:00Z","type":"reflection","task":"...","skills_used":[],"agents_used":[],"context_files":["..."],"tools_summary":{},"self_rating":{"effectiveness":4,"confidence":4,"notes":"..."}}' >> .claude/observability/reflections.jsonl
```

## Generating metrics

Run the aggregator (reads `events.jsonl`, prints a summary JSON):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .claude/hooks/observability_report.ps1          # all sessions
powershell -NoProfile -ExecutionPolicy Bypass -File .claude/hooks/observability_report.ps1 -Save     # also write metrics.json
powershell -NoProfile -ExecutionPolicy Bypass -File .claude/hooks/observability_report.ps1 -Session <id>  # one session
```

The summary reports: totals (events, sessions, turns, tool_calls, tool_time_ms), `tools_used`, `skills_used`, `agents_used`, `files_referenced` (with access counts), and `durations` (measured_calls, total_ms, and per-tool calls/total_ms/avg_ms/max_ms). When the user asks "how much am I using X" / "genera un report", run this and present it, then optionally cross-reference `reflections.jsonl` for the semantic side (effectiveness ratings, which context files mattered).

## When to activate

- **Explicit** — "monitora l'agente", "osservabilità", "metriche", "report", "quante volte ho usato X", `/agent-observability`.
- **Proactive reflection** — after finishing a substantive multi-step task, append a reflection (see above).
- **Report on request** — when asked about effectiveness/usage, run `observability_report.ps1` and summarize.

## Invariants

- **Hooks stay silent and non-blocking** — the logger only writes to file and exits 0; it must never print to stdout or block a turn.
- **One JSON object per line** in both `.jsonl` files (append-only, never rewrite).
- **UTC ISO-8601 timestamps** (`...Z`).
- **Honest reflections** — the whole point is measuring the agent; biased self-ratings defeat it.
- **Don't double-log** routine tool use manually — that's the hook's job.

## Notes / knobs

- The logger runs on both `PreToolUse` and `PostToolUse` so it can measure per-tool wall-clock duration (`dur_ms`). That is two hook invocations per tool call; if overhead ever matters, drop the `PreToolUse` registration and durations simply stop being recorded (everything else keeps working).
- Hooks are project-local (`.claude/settings.local.json`). To observe every project globally, move the same `hooks` block into `~/.claude/settings.json` and use an absolute script path.
