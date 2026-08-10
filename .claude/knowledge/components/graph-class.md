---
id: graph-class
title: Graph class and in-RAM model
type: component
tags: [ram, orchestration]
aliases: []
created: 2026-08-09
updated: 2026-08-10
status: active
---

# Graph class and in-RAM model
> Graph owns the in-RAM node map, the inbound index and meta, and persists on every mutation.

## Responsibility

`Graph` owns the in-RAM model and drives disk persistence on every mutation. There is no
separate commit step: `insert`, `add_edge` and `delete_node` each write through to `db/`
and rewrite `meta.dat` before returning.

## Where it lives

`graph_core/graph.h` (templates: `insert`, `traverse`) and `graph_core/graph.cpp`
(`add_edge`, `delete_node`, constructor). Domain structs are in
`graph_core/struct/domain_struct.h`.

## How it behaves

- State: `unordered_map<int, BaseNode*> nodes` (owns node lifetime), the reverse index
  `in_edges` ([[in-edges-index]]), a `MetaRecord meta`, and a `Logger` ([[logger]]).
- `BaseNode` is type-erased and holds the adjacency in `neighborgs`:
  `relation -> (neighbor_id -> EdgeRef)`. `EdgeRef` is `{id, weight, neighbor*, offset}`;
  `offset` is the edge's position on disk, which is what makes a weight overwrite O(1).
- `insert<T>` first tries the reuse path - pop an exact-size bin from the [[freelist]],
  recycle both the id and the disk region, write in place - and only appends a fresh id if
  no bin matched. Primitive versus COMPLEX writers are selected with `if constexpr`.
- `add_edge` is O(1) and does persist. A new `(start, relation, end)` triple goes through
  `persist_new_edge`; an existing one through `persist_edge_weight`. See [[decision-o1-add-edge]].
- `delete_node` drops the node's outbound entries from `in_edges`, uses the reverse index
  to erase inbound owners' edges, frees the RAM node, then tombstones and frees on disk
  ([[tombstoning]]).
- `delete_edge` removes one edge, by `(start, end, relation)` or by global edge id; the id
  overload resolves the id and delegates, so there is one delete path
  ([[decision-single-edge-delete-via-rewrite]]).
- `traverse<Policy>` is a single template; `bfs`/`dfs` are wrappers ([[traversal-policies]]).
- `ensure_loaded(id, action)` is the **single lazy-load path**: resident, else `read_node` and
  cache, else throw. Every entry point above uses it - it replaced six copies of the same block
  ([[decision-lazy-traversal]]).

## Contracts and constraints

- Neighbor pointers in `EdgeRef` are `nullptr` after a disk load until something re-resolves
  them - traversal must not assume they are live.
- RAM is mutated before the disk write, so a throw mid-persist leaves RAM ahead of disk.
  A restart re-reads the older on-disk state rather than a corrupted mix.
- `EdgeRef.offset` must stay aligned with disk; `update_node_edges` refreshes it whenever it
  relocates a node's edges.

## Links

- part of [[graph-core-architecture]] - the domain layer and orchestrator
- depends on [[persistence-io]] - every mutation writes through this layer
- depends on [[pod-layout]] - the record shapes it persists
- contains [[traversal-policies]] - the traverse template and its policies
- contains [[in-edges-index]] - the reverse index it owns and maintains
- uses [[logger]] - constructs `Logger("graph.log", LogLevel::DEBUG)`
- relates to [[tombstoning]] - how delete_node behaves on disk
- implements [[decision-o1-add-edge]] - the add_edge fast path
- implements [[decision-lazy-traversal]] - `ensure_loaded` is the shared lazy-load path
- implements [[decision-single-edge-delete-via-rewrite]] - both `delete_edge` overloads
- relates to [[reverse-index-node-granularity]] - the check `delete_edge` must do on `in_edges`
