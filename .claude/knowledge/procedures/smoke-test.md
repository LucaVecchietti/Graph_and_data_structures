---
id: smoke-test
title: Run the six-phase smoke test
type: procedure
tags: [test, regression]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Run the six-phase smoke test
> main.cpp is the only verification harness: six phases covering insert, reload, delete, reuse, compaction and edge chains.

## When to run this

After every change to the engine. `main.cpp` is the only verification harness in the project -
there is no automated test suite, so this is the regression suite.

## Preconditions

- A successful build, and the binary launched from `build/` - see [[build-and-run]].
- Windows only: `main.cpp` includes `<windows.h>` and ends with `system("pause")`.
- It wipes `db/` on startup, so no state carries over between runs.

## Steps

Run `.\build\graph.exe` from `build/` and read the six phases in order:

1. **Fresh writes** - three `insert<int>`, one `insert<ComplexRecord>` (an Athlete), two
   `add_edge` calls, then a BFS.
2. **Reload** - reopens the store and verifies persisted edges survived a restart.
3. **Delete and reuse** - `delete_node` followed by id and region reuse from the `nodes_4` bin.
4. **COMPLEX delete and reuse** - the `complex_<size>` bin, a recycled `prog_number`, and a
   rewritten sidecar ([[complex-nodes]]).
5. **Edge-space compaction** - 20 weight overwrites of one edge, asserting **zero** file
   growth through the `rel`/`edges` bin push-then-pop.
6. **Edge chains** - several edges on one relation, plus a mid-chain weight overwrite that
   must survive a reload ([[edge-record]]).

## Verification

Check the phase output against `db/`, `db/freelist/`, `db/attributes/` and the meta counters.
Phase 5 is the sharp one: any file growth there means the freelist reuse path regressed.

## If it goes wrong

Phases re-load persisted nodes by calling `add_edge` with a throwaway `"_load"` relation,
because [[traversal-policies]] does not lazy-load. If a phase seems to see no nodes, that trick
is usually what broke.

## Links

- part of [[build-and-run]] - the verification step of that procedure
- relates to [[graph-class]] - it exercises insert, add_edge, delete_node and bfs
- relates to [[freelist]] - phases 3 to 5 are the freelist regression guard
