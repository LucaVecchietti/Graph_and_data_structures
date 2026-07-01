---
node: graph-core
title: Graph Class & In-RAM Model
type: domain
tags: [graph, ram, traversal]
updated: 2026-07-01
---

# Graph Class & In-RAM Model

`Graph` (`graph.h/cpp`) owns the in-RAM model and orchestrates persistence.

## Facts

- Owns `unordered_map<int, BaseNode*>` and `MetaRecord meta`.
- Every `insert<T>` immediately persists via `io/graph_io.h`'s `write_node` (record → relations → index, all appended; `meta.dat` truncated and rewritten).
- Traversal is policy-based: `Graph::traverse<Policy>` is one template; `BFSPolicy`/`DFSPolicy` (`struct/functions_policies.h`) differ only in `Frontier = queue` vs `stack`. `bfs`/`dfs` are thin wrappers.
- **`add_edge` is RAM-only** (BUG-001): it does not re-flush `edges.dat` after the initial `write_node`, so restarting loses edges added after insert.

## Relations

- **part-of** → [[architecture-overview]]
- **depends-on** → [[persistence-io]]
- **documented-in** → docs/modules/graph_core.md
- **part-of** → [[Index]]

## Sources

- `graph_core/graph.h`, `graph_core/graph.cpp`
- `graph_core/struct/functions_policies.h`
