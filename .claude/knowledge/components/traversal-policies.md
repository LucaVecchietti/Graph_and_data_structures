---
id: traversal-policies
title: Policy-based BFS/DFS traversal
type: component
tags: [traversal]
aliases: []
created: 2026-08-09
updated: 2026-08-10
status: active
---

# Policy-based BFS/DFS traversal
> One traverse template with a swappable frontier policy; bfs and dfs are thin wrappers.

## Responsibility

Provide BFS and DFS from a single algorithm, with the frontier container as the only
difference between them.

## Where it lives

`graph_core/struct/functions_policies.h` (the policies) and `Graph::traverse` in
`graph_core/graph.h`.

## How it behaves

- `BFSPolicy` uses `Frontier = std::queue<int>`, `DFSPolicy` uses `std::stack<int>`; each
  exposes static `push`/`pop`/`empty`. Swapping the policy changes only the visit order.
- `Graph::traverse<Policy, NodeFn, EdgeFn>` is the one template; `bfs` and `dfs` are thin
  wrappers that pick a policy.
- A traversal follows a **single** relation type and fires `on_node(id)` and
  `on_edge(from, to, weight)`.
- Every node is materialised as it leaves the frontier, via `Graph::ensure_loaded` - so a walk
  of any depth works on a cold store. See [[decision-lazy-traversal]].

## Contracts and constraints

- A node that cannot be materialised is **fatal**, not skipped: `std::out_of_range` for a
  never-assigned start id, `std::runtime_error` for an unreadable record (a dangling edge into
  a tombstoned slot). Before 2026-08-10 traversal skipped silently instead, which truncated
  every cold walk at depth 1.
- A walk pulls the whole reachable component into RAM and `nodes` has no eviction, so memory
  only grows for the lifetime of the `Graph`.
- Policies are static structs, not virtuals, deliberately: the alternative was runtime
  indirection inside a hot loop.

## Links

- part of [[graph-class]] - `traverse` is a member template of Graph
- relates to [[edge-record]] - it walks the adjacency rebuilt from these chains
- implements [[decision-lazy-traversal]] - the per-pop lazy load lives in this template
- verified by [[smoke-test]] - phase 2 walks a 2-hop chain from a cold store
