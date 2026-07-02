---
node: in-edges-index
title: Inbound (Reverse) Edge Index
type: concept
tags: [ram, reverse-index, delete]
updated: 2026-07-01
---

# Inbound (Reverse) Edge Index

`Graph::in_edges` — a RAM-only reverse map letting `delete_node` find who points at a node in O(deg_in) instead of scanning all edges.

## Facts

- Type: `unordered_map<int, unordered_set<int>>` — `to_id → { owner node ids }` (`graph.h:26-30`).
- Built once at load by `build_in_edges` → `build_inbound_index(meta.next_id)` (O(N+E), skips tombstones) (`graph.cpp:52-58`).
- Maintained incrementally: `add_edge` inserts `start` into `in_edges[end]`; `delete_node` removes the deleted node's outbound entries and erases its inbound owners.
- **Never persisted** — rebuilt from disk on every load.

## Relations

- **part-of** → [[graph-core]]
- **depends-on** → [[persistence-io]]
- **relates-to** → [[edge-record]]

## Sources

- `graph_core/graph.h`, `graph_core/graph.cpp`, `graph_core/io/graph_io.h` (`build_inbound_index`)
