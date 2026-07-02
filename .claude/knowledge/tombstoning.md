---
node: tombstoning
title: Tombstoning (Logical Node Delete)
type: concept
tags: [delete, tombstone, id-recycling]
updated: 2026-07-01
---

# Tombstoning

A deleted node's `nodes.idx` slot survives (so its id can be recycled) while its data regions are zeroed and freed.

## Facts

- `NodeType::TOMBSTONE = 254` marks a logically-deleted slot (`pod_struct.h:24-29`).
- `delete_node_from_disk` zero-fills the on-disk `NodeRecord` / `NodeRelationList` / edge chunks, pushes those regions onto the [[freelist]] bins, and tombstones the `NodeIndex` (offset/relation_offset zeroed) — but keeps the fixed-width idx slot addressable.
- `read_node` on a tombstoned id **throws** (it is not a valid live node).
- Reuse (`write_node_in_freed_slot` / `write_complex_in_freed_slot`) overwrites the WHOLE `NodeIndex` in place, clearing the tombstone naturally.
- `build_inbound_index` skips tombstoned slots so zeroed edge regions are never mistaken for live edges.

## Relations

- **part-of** → [[persistence-io]]
- **relates-to** → [[freelist]]
- **relates-to** → [[pod-layout]]
- **relates-to** → [[complex-nodes]]

## Sources

- `graph_core/struct/pod_struct.h:24-29`, `graph_core/io/graph_io.{h,cpp}` (`delete_node_from_disk`)
