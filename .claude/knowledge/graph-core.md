---
node: graph-core
title: Graph Class & In-RAM Domain Model
type: domain
tags: [graph, ram, orchestration, domain]
updated: 2026-07-01
---

# Graph Class & In-RAM Domain Model

`Graph` (`graph.{h,cpp}`) owns the in-RAM model and drives disk persistence on every mutation. Domain structs live in `struct/domain_struct.h`.

## Facts

- Owns `unordered_map<int, BaseNode*> nodes` (node lifetime), `unordered_map<int, unordered_set<int>> in_edges` ([[in-edges-index]]), `MetaRecord meta`, and a `Logger` (`graph.h:23-34`).
- `BaseNode` is type-erased and holds the adjacency `neighborgs`: `unordered_map<string relation, unordered_map<int neighbor_id, EdgeRef>>` (`domain_struct.h:41-45`). `EdgeRef{id, weight, neighbor*, offset}` — `offset` enables O(1) weight overwrite; `neighbor` is nullptr until re-linked after a disk load (`domain_struct.h:29-35`).
- **`insert<T>`** (forwarding ref): tries the freelist reuse path first — pop an exact-size bin, recycle the id + region, write in place; else append a fresh id. Primitive vs COMPLEX writers are chosen with `if constexpr` (`graph.h:50-126`). Persists then `write_meta`.
- **`add_edge`** is O(1) and **DOES persist** (BUG-001 is fixed — see [[doc-drift-caveats]]): a new `(start,type,end)` → `persist_new_edge` (append/reuse + splice at chain head + in-place relation line); an existing triple → in-place `persist_edge_weight`. Lazy-loads absent endpoints via `read_node`; maintains `in_edges` (`graph.cpp:66-174`).
- **`delete_node`**: drops the node's outbound entries from `in_edges`, uses the reverse index to erase inbound owners' edges (`update_node_edges`), frees the node, then `delete_node_from_disk` (tombstone + freelist push) and rewrites `meta` (`graph.cpp:180-295`).
- **`traverse<Policy>`** is one template; `bfs`/`dfs` are wrappers ([[traversal-policies]]). It follows a single relation type and does **not** lazy-load — absent ids are silently skipped (`graph.h:151-200`).

## Relations

- **part-of** → [[architecture-overview]]
- **contains** → [[traversal-policies]]
- **contains** → [[in-edges-index]]
- **depends-on** → [[persistence-io]]
- **depends-on** → [[pod-layout]]
- **relates-to** → [[logger]]
- **relates-to** → [[smoke-test]]
- **relates-to** → [[doc-drift-caveats]]
- **documented-in** → docs/modules/graph_core.md

## Sources

- `graph_core/graph.h`, `graph_core/graph.cpp`, `graph_core/struct/domain_struct.h`
