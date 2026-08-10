---
id: in-edges-index
title: Inbound edge index
type: concept
tags: [ram, delete]
aliases: []
created: 2026-08-09
updated: 2026-08-10
status: active
---

# Inbound edge index
> A RAM-only reverse map from node id to the nodes pointing at it, so delete_node is O(deg_in).

## What it is

`Graph::in_edges` - a RAM-only reverse map from a node id to the set of nodes pointing at it,
so `delete_node` can find inbound owners in O(deg_in) instead of scanning `edges.dat`.

## How it works

- Type: `unordered_map<int, unordered_set<int>>`, `to_id -> {owner ids}`.
- Built once at load by `build_in_edges` calling `build_inbound_index(meta.next_id)`: an
  O(N+E) scan that reads only live nodes' relation lists, skipping tombstones so zeroed
  regions never register as edges.
- Maintained incrementally afterwards: `add_edge` inserts `start` into `in_edges[end]`;
  `delete_node` removes the deleted node's outbound entries and erases its inbound owners;
  `delete_edge` removes `start` only when no relation of it points at `end` any more.
- Never persisted - it is rebuilt from disk on every load.
- **The key is a node pair, with no relation in it.** An entry means "this source has at least
  one edge here", so a per-relation removal may only erase it once that count hits zero -
  the trap in [[reverse-index-node-granularity]].

## Why it matters here

The adjacency model is outbound-only (`BaseNode::neighborgs[relation][to_id]`), so "who
points at X?" is otherwise unanswerable without a full scan. Without this, deleting a node
would leave dangling neighbours in other nodes that survive a reload.

## Links

- part of [[graph-class]] - Graph owns and maintains it
- depends on [[persistence-io]] - `build_inbound_index` populates it at load
- specified by [[decision-in-ram-inbound-index]] - why it is not persisted
- relates to [[tombstoning]] - the scan must skip tombstoned slots to stay correct
- causes [[reverse-index-node-granularity]] - the node-to-node key is the trap's root cause
