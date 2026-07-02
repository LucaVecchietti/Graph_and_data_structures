---
node: edge-record
title: Edge Record & Doubly-Linked Chains
type: component
tags: [edges, disk-format, linked-list, o1]
updated: 2026-07-01
---

# Edge Record & Doubly-Linked Chains

The `Edge` POD and the doubly-linked list that lets a new edge splice into a relation in O(1).

## Facts

- `Edge {uint64 id; int64 weight; uint64 to_node, from_node, prev_offset, next_offset}` (`pod_struct.h:133-146`).
- All edges of one `(node, relation)` pair form a **doubly-linked list** in `edges.dat`. A new edge is spliced at the head in O(1) (`persist_new_edge`): append/reuse, set `prev=0` / `next=old head`, patch the old head's `prev_offset`.
- Read walks the chain from the relation line's `edge_offset`, hopping by `next_offset`, bounded by `edge_count` (`read_typed_node`, `graph_io.h:463-475`).
- `write_edge_chain_at` writes a contiguous doubly-linked run for the initial insert path (`graph_io.h:167-181`).
- `weight` is `int64` on disk but `int` in the RAM `EdgeRef`.

## Relations

- **part-of** → [[pod-layout]]
- **relates-to** → [[relation-batch]]
- **relates-to** → [[traversal-policies]]
- **relates-to** → [[in-edges-index]]
- **documented-in** → docs/modules/db.md

## Sources

- `graph_core/struct/pod_struct.h:133-146`, `graph_core/io/graph_io.h` (`persist_new_edge`, `write_edge_chain_at`)
