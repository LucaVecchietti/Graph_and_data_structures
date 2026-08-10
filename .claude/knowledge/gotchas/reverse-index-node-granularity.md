---
id: reverse-index-node-granularity
title: The reverse edge index has no relation granularity
type: gotcha
tags: [delete, trap]
aliases: [BUG-018]
created: 2026-08-10
updated: 2026-08-10
status: active
---

# The reverse edge index has no relation granularity
> in_edges maps node to node, so removing one relation must not drop the source unless no other relation still points there.

## Symptom

Two edges between the same pair of nodes on different relations - `0 --road--> 1` and
`0 --train--> 1`. After `delete_edge(0, 1, "road")`, a later `delete_node(1)` does **not** clean
the surviving `train` edge. Node 0 keeps a neighbour pointing at a tombstoned slot; on reload that
edge is back in its adjacency, and if `insert` has meanwhile recycled id 1 it silently points at a
**different node**. No error is raised at any step. Since 2026-08-10 the same edge instead makes a
traversal throw, because [[decision-lazy-traversal]] tries to materialise the tombstoned slot.

## Root cause

[[in-edges-index]] is `unordered_map<int target, unordered_set<int> sources>`: the key is a node
pair, there is no relation in it. Removing a single relation is therefore **not** the same as the
source no longer pointing at the target, but the first version of `delete_edge` did an
unconditional `in_edges[end].erase(start)`.

`delete_node` never had the bug: there the source disappears entirely, so erasing it from every
target's set is correct. The trap only appears once a path removes *one* relation, which is why it
arrived with [[decision-single-edge-delete-via-rewrite]].

## Fix

Before touching the index, check whether **any** remaining relation of the node still points at
the target, and only then erase (`graph_core/graph.cpp`, in `delete_edge`). O(relation types of
the node). Use `find()` rather than `in_edges[end]`, so a target that was never seen does not get
an empty set created for it.

## How to avoid it

Any future per-relation edge removal (notably the planned O(1) unlink) has to repeat this check.
The rule of thumb: an entry in `in_edges` means "this source has at least one edge here", so it
may only be removed when the count of such edges reaches zero.

## Links

- caused by [[in-edges-index]] - the node-to-node key is where the ambiguity comes from
- caused by [[decision-single-edge-delete-via-rewrite]] - the first path to remove a single relation
- relates to [[decision-in-ram-inbound-index]] - the decision that chose this index shape
- relates to [[decision-lazy-traversal]] - a dangling edge now throws mid-walk instead of being skipped
- relates to [[tombstoning]] - the dangling edge points at a tombstoned slot whose id can be recycled
