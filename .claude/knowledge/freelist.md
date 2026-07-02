---
node: freelist
title: Size-Segregated Freelists
type: concept
tags: [freelist, reuse, allocation, disk]
updated: 2026-07-01
---

# Size-Segregated Freelists

Reclaimed disk regions are recycled via LIFO bins segregated by EXACT size, so every reuse is an exact fit — no scanning, no rewrite, no wasted bytes.

## Facts

- One bin file per distinct free-region size: `db/freelist/<prefix>_<size>.dat` (`freelist_bin_path`, `graph_io.h:498-501`).
- `push` (on delete/orphan) = append one record → O(1). `pop` (on insert/reuse) = read last record + `resize_file` shrink → O(1) (`write_free_offset`/`pop_free_offset`, `graph_io.h:534-582`).
- Prefixes: `nodes` (`NodeFreeOffset`, sizes ∈ {1,4,8} for primitives), `rel` (`RelationNodeListFreeOffset` — single size class, every [[relation-batch]] is 2213B), `edges` (`BatchOfEdgesFreeOffset`, size = `edge_count * sizeof(Edge)`), `complex` (per-`type_label`-length size classes → [[complex-nodes]]).
- `db/freelist/json_prog.dat` — flat LIFO stack of recycled COMPLEX `prog_number`s (a sidecar file has no offset; reclaim = delete file + recycle number).
- `insert`/`add_edge` pop; `delete_node`/`update_node_edges` push. Backed by [[tombstoning]] for node id slots.

## Relations

- **part-of** → [[persistence-io]]
- **depends-on** → [[pod-layout]]
- **relates-to** → [[tombstoning]]
- **relates-to** → [[complex-nodes]]

## Sources

- `graph_core/io/graph_io.h` (`freelist_bin_path`, `write_free_offset`, `pop_free_offset`), `db/freelist/`
