---
id: edge-record
title: Edge record and doubly-linked chains
type: component
tags: [disk-format, edges]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Edge record and doubly-linked chains
> The 48-byte Edge POD and the per-(node,relation) doubly-linked list that makes add_edge O(1).

## Responsibility

Represent one directed, weighted, typed edge on disk, and chain the edges of a
`(node, relation)` pair so a new edge can be attached without rewriting anything.

## Where it lives

`Edge` in `graph_core/struct/pod_struct.h`; the chain logic in `graph_core/io/graph_io.h`
(`persist_new_edge`, `write_edge_chain_at`, `free_edge_chain`) and the read walk in
`read_typed_node`.

## How it behaves

- `Edge {uint64 id; int64 weight; uint64 to_node, from_node, prev_offset, next_offset}` =
  **48 bytes**.
- All edges of one `(node, relation)` pair form a doubly-linked list in `edges.dat`. A new
  edge is spliced at the head in O(1): allocate a slot (freelist pop or append), write
  `prev = 0` and `next = old head`, then patch the old head's `prev_offset`.
- Reading walks from the relation line's `edge_offset`, hopping `next_offset`, bounded by
  `edge_count` - so the walk is robust to non-contiguous splices.
- `write_edge_chain_at` writes a contiguous chained run, used by the initial insert path.
- `id` is globally unique and stable: it is sourced from `MetaRecord.next_edge_id`, stored in
  the RAM `EdgeRef`, and survives reloads and whole-node rewrites. Edge ids are never
  recycled, even when an edge slot is.

## Contracts and constraints

- `weight` is `int64` on disk but plain `int` in the RAM `EdgeRef`.
- Because the splice scatters edges, freed edges are recycled one 48-byte slot at a time,
  which is why the `edges` [[freelist]] bin has a single size class.
- There is no single-edge delete yet; removing edges goes through the whole-node
  `update_node_edges` rewrite.

## Links

- part of [[pod-layout]] - one of the on-disk records
- implements [[decision-fixed-width-relation-batch]] - the chain was introduced by that format change
- used by [[decision-o1-add-edge]] - the O(1) splice is what that decision exploits
- relates to [[relation-batch]] - the relation line stores the chain head and length
