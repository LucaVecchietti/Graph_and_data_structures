---
id: project-docs
title: The docs/ tree
type: reference
tags: [docs]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# The docs/ tree
> docs/ holds the canonical architecture, module, roadmap and legacy logs, under the standard in docs/STANDARD.md.

## Source

`docs/` in this repository. Index at `docs/README.md`; the writing standard every document
must follow is `docs/STANDARD.md`.

## Why it is worth keeping

It is the canonical human-facing documentation, and it carries reasoning that the source code
does not: why a format was chosen, what was tried and rejected, and what is still missing.
This knowledge graph summarises it; `docs/` holds the long form.

## Key points

- `docs/architecture/overview.md` - system components and data flow.
- `docs/modules/graph_core.md`, `docs/modules/db.md`, `docs/modules/data_structures.md` -
  per-module detail; `db.md` is the reference for the on-disk format.
- `docs/ROADMAP.md` - what the engine can do today, what is half-done, and the prioritised
  TODO list. The most current of the human-facing documents.
- `docs/legacy/design_decisions.md` - dated decision log with context, alternatives and
  consequences; the source for this graph's decision nodes.
- `docs/legacy/api_changes.md` - before/after of signature and behaviour changes.
- `docs/legacy/known_bugs.md` - `BUG-NNN` ids, never reused, cited in commits and PRs.
- Every document carries a metadata table (Tipo / Lingua / Ultimo aggiornamento / Commit di
  riferimento / Mirror); `Ultimo aggiornamento` is an absolute date.

## Caveats

- The prose is Italian-flavoured English and the decision log is largely in Italian; the
  Italian mirror under `docs/it/` is only created on explicit request.
- `docs/legacy/known_bugs.md` lags the code - see [[docs-drift-vs-code]].
- The `docs-keeper` skill, not this graph, is responsible for maintaining `docs/`.

## Links

- documents [[project-overview]] - the long-form version of this graph's subject
- relates to [[docs-drift-vs-code]] - parts of this tree are stale
