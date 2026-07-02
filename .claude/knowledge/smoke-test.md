---
node: smoke-test
title: Smoke Test (main.cpp, 6 phases)
type: workflow
tags: [test, regression, main]
updated: 2026-07-01
---

# Smoke Test

`main.cpp` — the only verification harness (no automated suite). Hand-driven, six phases, each exercising a specific path. Wipes `db/` at start (`main.cpp:24`) and ends with `system("pause")`; Windows-only (`<windows.h>`).

## Facts

- **Phase 1** — fresh writes: `insert<int>` ×3 + `insert<ComplexRecord>` (Athlete) + `add_edge` (road/knows) + BFS.
- **Phase 2** — reload + verify persisted edges survive a restart — the **regression test for BUG-001** (see [[doc-drift-caveats]]).
- **Phase 3** — `delete_node` + freelist id/region reuse (`nodes_4.dat` bin).
- **Phase 4** — COMPLEX delete + reuse: `complex_<size>` bin + recycled `prog_number` + sidecar rewrite ([[complex-nodes]]).
- **Phase 5** — edge-space compaction: 20 weight-overwrites of one edge, asserting **zero file growth** via rel/edges bin push+pop.
- **Phase 6** — multi-edge chain on one relation (O(1) add), mid-chain weight overwrite survives reload ([[edge-record]]).
- Traversal doesn't lazy-load, so phases re-load persisted nodes into RAM by calling `add_edge` with a throwaway `"_load"` relation.

## Relations

- **part-of** → [[build-and-run]]
- **relates-to** → [[graph-core]]

## Sources

- `main.cpp`
