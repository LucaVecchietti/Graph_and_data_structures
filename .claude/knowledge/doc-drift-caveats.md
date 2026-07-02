---
node: doc-drift-caveats
title: Doc Drift — CLAUDE.md vs. Current Code
type: reference
tags: [caveat, drift, bugs]
updated: 2026-07-01
---

# Doc Drift — CLAUDE.md vs. Current Code

`CLAUDE.md` and `docs/legacy/known_bugs.md` describe an OLDER state than the code. Verify these areas against source, not against the docs.

## Facts

- **BUG-001 ("add_edge is RAM-only") is FIXED.** `add_edge` persists via `persist_new_edge` / `persist_edge_weight`; edges survive a restart. [[smoke-test]] Phase 2 is the regression guard.
- **BUG-003 ("reconstruct_neighbors is a stub") and BUG-004 ("node_form_pod typo") no longer apply.** Both functions were **removed** — they never had access to the on-disk tail or `edges.dat`. Real POD→domain reconstruction is `read_typed_node` ([[persistence-io]], `node_odt.h:84-88`).
- `read_node` DOES rebuild adjacency (edges are read + chained); only the `EdgeRef.neighbor` pointers stay `nullptr` until re-resolved.
- Features present in code but under-described in CLAUDE.md: size-segregated [[freelist]], [[tombstoning]] + id recycling, the [[in-edges-index]] reverse index, O(1) doubly-linked [[edge-record]] chains, and implemented [[complex-nodes]] with JSON sidecars.
- The `docs-keeper` skill owns bringing `docs/` back in line; this node is the fast heads-up for Claude.

## Relations

- **relates-to** → [[graph-core]]
- **relates-to** → [[persistence-io]]
- **relates-to** → [[odt-layer]]
- **documented-in** → docs/legacy/known_bugs.md

## Sources

- `graph_core/graph.cpp`, `graph_core/io/graph_io.h`, `graph_core/odt/node_odt.h:84-88`, `CLAUDE.md`
