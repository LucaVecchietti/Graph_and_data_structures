---
id: traversal-policies
title: Policy-based BFS/DFS traversal
type: component
tags: [traversal]
aliases: []
created: 2026-08-09
updated: 2026-08-09
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

## Contracts and constraints

- Traversal does **not** lazy-load: ids that are not already in the RAM `nodes` map are
  silently skipped. `main.cpp` works around this by forcing loads with a throwaway `"_load"`
  relation before traversing - see [[smoke-test]].
- Policies are static structs, not virtuals, deliberately: the alternative was runtime
  indirection inside a hot loop.

## Links

- part of [[graph-class]] - `traverse` is a member template of Graph
- relates to [[edge-record]] - it walks the adjacency rebuilt from these chains
- relates to [[smoke-test]] - the `_load` trick exists because of the no-lazy-load rule
