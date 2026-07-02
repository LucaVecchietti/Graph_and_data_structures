---
node: traversal-policies
title: Traversal Policies (BFS/DFS)
type: component
tags: [traversal, policy, bfs, dfs]
updated: 2026-07-01
---

# Traversal Policies

Policy-based traversal: one algorithm, swappable frontier.

## Facts

- `struct/functions_policies.h` defines `BFSPolicy` (`Frontier = std::queue<int>`) and `DFSPolicy` (`Frontier = std::stack<int>`), each exposing `push`/`pop`/`empty` (`functions_policies.h:12-36`).
- `Graph::traverse<Policy, NodeFn, EdgeFn>` is a single template; swapping the policy changes only the visit order. `bfs`/`dfs` are thin wrappers (`graph.h:151-200`).
- Traversal follows **one relation type** and fires `on_node(idx)` / `on_edge(from,to,weight)`. It does **not** lazy-load: ids not already in the RAM `nodes` map are skipped.

## Relations

- **part-of** → [[graph-core]]
- **relates-to** → [[edge-record]]
- **documented-in** → docs/modules/graph_core.md

## Sources

- `graph_core/struct/functions_policies.h`, `graph_core/graph.h`
