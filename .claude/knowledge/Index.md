---
node: Index
title: pointer_graphs — Knowledge Graph Root
type: root
tags: [root, index]
updated: 2026-07-01
---

# pointer_graphs — Knowledge Graph

**Purpose.** This is Claude's internal map of the `pointer_graphs` project: a C++17 persistent graph store. Reload the project's mental model by starting here and following the `[[wikilinks]]`.

**How to read it.** Start at this root, walk into a cluster, follow typed relations (`part-of`, `depends-on`, …). Every node is reachable from here.

## Clusters

- [[architecture-overview]] — the three-layer separation (Domain / POD / ODT) and how data flows.
- [[graph-core]] — the `Graph` class and the in-RAM model.
- [[persistence-io]] — append-only disk format, `db/` files, persistence quirks.
- [[build-and-run]] — how to build, run, and the working-directory trap.

## Node registry

- [[architecture-overview]] — domain vs. POD vs. ODT layers.
- [[graph-core]] — in-RAM `unordered_map<int, BaseNode*>`, insert/add_edge/traverse.
- [[persistence-io]] — `write_node`, append-only `nodes.dat`/`nodes.idx`/`edges.dat`, `meta.dat` rewrite.
- [[build-and-run]] — CMake + Ninja + ucrt64, `system("pause")` smoke test, `DB_PATH` relative-path trap.
- [[glossary-load-bearing-typos]] — misspellings that are load-bearing (`neighborgs`, `data_tructures`, `costants`).

## Maintenance

This graph is maintained by the `knowledge-graph` skill. New knowledge → add/update a node, write the inverse relation on its target, and register it here.
