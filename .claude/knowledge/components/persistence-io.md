---
id: persistence-io
title: Persistence and I/O layer
type: component
tags: [io, disk]
aliases: []
created: 2026-08-09
updated: 2026-08-10
status: active
---

# Persistence and I/O layer
> io/graph_io and io/io_utils: the read/write paths between POD records and the db/ files.

## Responsibility

Move bytes between POD records and the files under `db/`: `nodes.dat`, `nodes.idx`,
`edges.dat`, `meta.dat`, plus `db/freelist/` and `db/attributes/`.

## Where it lives

`graph_core/io/graph_io.h` (templates and the hot paths), `graph_core/io/graph_io.cpp`
(`read_relation_node_list`, `update_node_edges`, `build_inbound_index`,
`delete_node_from_disk`), `graph_core/io/io_utils.{h,cpp}` (primitives).

## How it behaves

- `write_pod`/`read_pod` require `is_trivially_copyable_v<T>`; alongside them sit
  `write_string`/`read_string` (length-prefixed) and `write_offset`/`read_offset`.
- `write_node` does a single append-open on `nodes.dat` so that `tellp()` stays correct,
  writes the record (or, for COMPLEX, header plus two strings), then the relation batch,
  then the `NodeIndex` into `nodes.idx`.
- `read_node`/`read_typed_node` read the record, then the relation batch, then walk each
  edge chain by `next_offset`, bounded by the line's `edge_count`. Adjacency is rebuilt;
  only the `EdgeRef.neighbor` pointers are left `nullptr`.
- `read_relation_node_list` reads a node's WHOLE relation chain, hopping batch `next_offset`
  until 0 and bounded by `RELATION_MAX_BATCHES`. Its optional `batch_offsets` out-param
  reports every batch visited - the reclaim paths need one freelist record per batch, and
  `NodeIndex` only records where the chain starts. See [[decision-relation-batch-chaining]].
- O(1) edge operations: `persist_new_edge` allocates a slot (freelist pop or append),
  splices at the chain head, patches the old head's `prev_offset` and updates one
  fixed-width relation line in place. If the relation type is new and the last batch of the
  chain is full, it allocates a batch and links it with one 8-byte write to `next_offset`.
  `persist_edge_weight` is an 8-byte in-place write.
- `update_node_edges` is the whole-node rewrite path, used by `delete_node`'s inbound cleanup
  and by both `delete_edge` overloads. It pushes every old batch and edge onto the [[freelist]] bins and pops
  exact-size bins for the new ones, which is what makes a weight overwrite cost zero growth.
  For a multi-batch chain it decides where every batch lands before writing any of them,
  because each `next_offset` needs the following batch's offset.
- `build_inbound_index` does an O(N+E) scan of live slots (skipping tombstones) to rebuild
  [[in-edges-index]] at load.
- `find_edge_by_id` resolves a global edge id to `EdgeLocation{from_node, to_node, relation}` by
  the same live-node sweep. There is no id-to-offset index, and a flat scan of `edges.dat` would
  be unsound: a freed 48-byte slot is zeroed and reads back as `id = 0`, colliding with the real
  edge id 0. Used by `delete_edge(edge_id)` ([[decision-single-edge-delete-via-rewrite]]).
- `nodes.idx` is fixed-width, so id lookup is `seekg(id * sizeof(NodeIndex))`.

## Contracts and constraints

- Data files are append-or-reuse; `meta.dat` is truncated and rewritten on every operation.
- `nodes.idx` is written in place (this is the one file that is not append-only).
- Streams are opened `in|out` rather than `app`, because on Windows `seekp` is ignored in
  append mode.
- Nothing is fsynced and no write is atomic - a crash mid-write can leave torn state.

## Links

- part of [[graph-core-architecture]] - the I/O layer
- implements [[decision-relation-batch-chaining]] - the chain walk and the batch allocation live here
- depends on [[odt-layer]] - receives PODs already translated from domain structs
- depends on [[pod-layout]] - the record layouts it reads and writes
- depends on [[type-registry]] - dispatches on `NodeType` and on-disk payload sizes
- used by [[graph-class]] - every mutation goes through here
- contains [[freelist]] - bin push/pop live in this layer
- contains [[tombstoning]] - `delete_node_from_disk` implements it
- uses [[logger]] - writes to `graph_io.log`
