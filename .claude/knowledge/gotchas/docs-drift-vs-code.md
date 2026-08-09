---
id: docs-drift-vs-code
title: CLAUDE.md and known_bugs.md lag the code
type: gotcha
tags: [docs, trap]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# CLAUDE.md and known_bugs.md lag the code
> CLAUDE.md still describes bugs that are fixed and features that no longer exist - verify against source.

## Symptom

You follow `CLAUDE.md` or `docs/legacy/known_bugs.md`, work around a documented bug, and find
the workaround is unnecessary - or you look for a documented function and it does not exist.

## Root cause

Those documents describe an older state of the codebase than the source does. Concretely:

- **BUG-001, "add_edge is RAM-only", is fixed.** `add_edge` persists through
  `persist_new_edge`/`persist_edge_weight`, and edges survive a restart. [[smoke-test]]
  phase 2 is the regression guard.
- **BUG-003 ("reconstruct_neighbors is a stub") and BUG-004 ("node_form_pod typo") no longer
  apply**, because both functions were **removed** rather than fixed - they never had access
  to the on-disk tail or to `edges.dat`. Real POD-to-domain reconstruction is `read_typed_node`.
- `read_node` **does** rebuild adjacency; only the `EdgeRef.neighbor` pointers stay `nullptr`
  until re-resolved.
- Features present in code but thin or absent in `CLAUDE.md`: the size-segregated [[freelist]],
  [[tombstoning]] with id recycling, the [[in-edges-index]], the O(1) doubly-linked chains in
  [[edge-record]], and fully implemented [[complex-nodes]] with JSON sidecars.

## Fix

Verify against source before acting on either document. `docs/ROADMAP.md` is the more current
of the human-facing docs; the `docs-keeper` skill owns bringing the rest back into line.

## How to avoid it

Read this graph or the source first, and treat `CLAUDE.md`'s bug list as historical until it
is refreshed.

## Links

- relates to [[odt-layer]] - the two removed helpers are documented as if they still exist
- relates to [[graph-class]] - add_edge's persistence is the biggest drift
- documented in [[project-docs]] - the tree that needs the refresh
