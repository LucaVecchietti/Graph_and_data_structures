---
id: project-overview
title: pointer_graphs - project overview
type: moc
tags: [architecture]
aliases: []
created: 2026-08-09
updated: 2026-08-10
status: active
---

# pointer_graphs - project overview
> Root map of the pointer_graphs C++17 persistent graph engine: layers, storage format, and how to build and exercise it.

`pointer_graphs` is an embedded, single-threaded, persistent graph engine written in C++17:
typed and weighted directed edges, immediate write-through persistence to a hand-rolled
binary format under `db/`, O(1) edge add and weight overwrite, freelist-based space
reclamation, and BFS/DFS traversal. There is no query layer, no durability guarantees and
no test suite.

## Start here

- [[graph-core-architecture]] - the four layers everything else hangs off; read this before touching code.
- [[docs-drift-vs-code]] - `CLAUDE.md` and the legacy bug log describe an older state than the source. Read before trusting either.
- [[build-and-run]] - how to compile and exercise the engine, plus the one trap that silently breaks persistence.

## The area in detail

**Engine internals** - [[graph-core-architecture]] fans out to [[graph-class]], [[pod-layout]],
[[odt-layer]] and [[persistence-io]], plus the storage concepts [[freelist]], [[tombstoning]],
[[in-edges-index]] and [[complex-nodes]].

**Working with it** - [[build-and-run]] and [[smoke-test]]. The traps: [[db-path-relative-to-cwd]],
[[load-bearing-misspellings]], [[docs-drift-vs-code]], [[reverse-index-node-granularity]].

**Historical / out of scope** - [[legacy-c-prototypes]] are early C prototypes not in the build.

## Open questions

- The on-disk format has no magic, version or checksum, and is host-byte-order dependent - which
  is also why a relation chain walk needs the `RELATION_MAX_BATCHES` bound
  ([[decision-relation-batch-chaining]]).
- `RELATION_LINES_PER_BATCH` is still 8, so a node pays 2213 bytes per started group of 8
  relation types; lowering it would be a schema break.
- Deleting a single edge **exists** but rides the whole-node rewrite, so it is O(deg) and grows
  `edges.dat` ([[decision-single-edge-delete-via-rewrite]]); the O(1) unlink, an in-place node
  payload update and a query layer are all still missing (see [[project-docs]] for the roadmap).
- A traversal materialises the whole reachable component and `nodes` has no eviction
  ([[decision-lazy-traversal]]), so RAM only grows for the lifetime of a `Graph`.

## Links

- contains [[graph-core-architecture]] - the engine area map
- relates to [[build-and-run]] - how to exercise what the graph describes
- relates to [[smoke-test]] - the only verification harness
- relates to [[load-bearing-misspellings]] - project-wide naming trap
- relates to [[legacy-c-prototypes]] - code in the repo that is deliberately not built
- documented in [[project-docs]] - the human-facing `docs/` tree
