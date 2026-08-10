---
id: smoke-test
title: Run the seven-phase smoke test
type: procedure
tags: [test, regression]
aliases: []
created: 2026-08-09
updated: 2026-08-10
status: active
---

# Run the seven-phase smoke test
> main.cpp is the only verification harness: seven self-checking phases from insert to relation-batch chaining.

## When to run this

After every change to the engine. `main.cpp` is the only verification harness in the project -
there is no automated test suite, so this is the regression suite.

## Preconditions

- A successful build, and the binary launched from `build/` - see [[build-and-run]].
- Windows only: `main.cpp` includes `<windows.h>` and ends with `system("pause")`.
- It wipes `db/` on startup, so no state carries over between runs.

## Steps

Run `.\build\graph.exe` from `build/`. Since 2026-08-10 the phases **self-check**: each one
prints `[PASS]`/`[FAIL]` with got-vs-want, and the run ends with a verdict and a non-zero exit
code if anything failed - no log eyeballing.

1. **Fresh writes** - inserts, edges and BFS with everything resident: the baseline the cold
   phases are compared against.
2. **Cold reload + lazy load** - the discriminating phase. A brand-new `Graph` has an empty node
   map, so a walk has to materialise nodes from disk: case (a) depth 1, case (b) depth 2, plus a
   never-assigned start id that must throw. See [[decision-lazy-traversal]].
3. **Delete and reuse** - `delete_node` followed by id and region reuse from the `nodes_4` bin.
4. **COMPLEX delete and reuse** - the `complex_<size>` bin, a recycled `prog_number`, and a
   rewritten sidecar ([[complex-nodes]]).
5. **Edge-space compaction** - 20 weight overwrites of one edge, asserting **zero** file growth.
6. **Edge chains** - several edges on one relation, plus a mid-chain weight overwrite that must
   survive a reload ([[edge-record]]).
7. **Relation-batch chaining** - a hub with 17 relation types (8 + 8 + 1 batches), verified before
   a reload, after a reload, and after a `delete_node` rewrites the whole chain
   ([[decision-relation-batch-chaining]]).

## Verification

Read the verdict, then cross-check `db/`, `db/freelist/`, `db/attributes/` and the meta counters.
Phase 5 is the sharp one: any file growth means the freelist reuse path regressed. Phase 7's log
should show two `chained batch head` lines reusing `rel` bin regions rather than appending.

## If it goes wrong

Older versions of this harness forced nodes into RAM with a throwaway `add_edge(x, y, "_load")`
relation, because traversal did not lazy-load. That trick is **gone** as of 2026-08-10 - if you
find it in a phase, it is stale code that also pollutes the store with fake edges.

**Known coverage gap:** no phase exercises `delete_edge` (either overload) or the reverse-index
check it has to do - see [[decision-single-edge-delete-via-rewrite]] and
[[reverse-index-node-granularity]]. Both were verified with a throwaway harness only.

## Links

- part of [[build-and-run]] - the verification step of that procedure
- relates to [[graph-class]] - it exercises insert, add_edge, delete_node and bfs
- relates to [[freelist]] - phases 3 to 5 are the freelist regression guard
- verifies [[decision-relation-batch-chaining]] - phase 7 is its regression guard
- verifies [[decision-lazy-traversal]] - phase 2 is its regression guard
