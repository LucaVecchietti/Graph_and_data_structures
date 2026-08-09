---
id: tombstoning
title: Tombstoning of deleted nodes
type: concept
tags: [delete, disk]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Tombstoning of deleted nodes
> A deleted node keeps its fixed-width nodes.idx slot so its id can be recycled, while its data regions are zeroed and freed.

## What it is

Logical node deletion: the node's `nodes.idx` slot survives so its id can be recycled, while
all of its data regions are zeroed and pushed onto the [[freelist]].

## How it works

- `NodeType::TOMBSTONE = 254` marks a logically deleted slot.
- `delete_node_from_disk` zero-fills the on-disk `NodeRecord`, the relation batch and every
  edge chunk (via `zero_region`), pushes those regions onto their bins, then writes a
  `NodeIndex` with `type_id = TOMBSTONE` and zeroed offsets - keeping the `id` so the
  fixed-width slot stays addressable.
- `read_node` on a tombstoned id **throws**, which is the point: a dangling reference fails
  loudly instead of returning garbage from dead bytes.
- Reuse (`write_node_in_freed_slot` / `write_complex_in_freed_slot`) overwrites the whole
  `NodeIndex` in place, so the tombstone clears naturally.
- `build_inbound_index` skips tombstoned slots, so zeroed edge regions are never mistaken for
  live edges.

## Why it matters here

Zeroing the slot entirely was not an option: `id = 0` is a valid node. Tombstoning is what
lets the index stay fixed-width (and therefore O(1) addressable) while still supporting delete.

## Links

- part of [[persistence-io]] - implemented by `delete_node_from_disk`
- depends on [[freelist]] - the freed regions and the recycled id go onto its bins
- relates to [[pod-layout]] - `NodeType::TOMBSTONE` is a POD enum value
- relates to [[complex-nodes]] - the COMPLEX branch also removes the sidecar and recycles its prog_number
