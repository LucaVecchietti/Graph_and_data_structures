---
node: persistence-io
title: Persistence & I/O Layer
type: domain
tags: [io, disk, persistence]
updated: 2026-07-01
---

# Persistence & I/O Layer

`io/graph_io.{h,cpp}` + `io/io_utils.{h,cpp}`: the read/write paths between POD records and the `db/` files. Append-only for data; `meta.dat` fully rewritten each op.

## Facts

- `write_pod`/`read_pod` require `is_trivially_copyable_v<T>` (`io_utils.h:17-35`); plus `write_string`/`read_string` (length-prefixed) and `write_offset`/`read_offset`.
- **`write_node`**: single append-open on `nodes.dat` (so `tellp()` is correct), writes the record (or, for COMPLEX, header + 2 strings), then the relation batch, then the `NodeIndex` to `nodes.idx` (`graph_io.h:250-289`).
- **`read_node`/`read_typed_node`**: reads `NodeRecord<T>` (or `read_complex`), then the relation batch, then walks each edge chain by `next_offset` bounded by `edge_count`. Neighbor pointers are left `nullptr` (must be re-resolved). It WORKS — `reconstruct_neighbors`/`node_form_pod` were **removed**, not stubs (see [[doc-drift-caveats]]) (`graph_io.h:422-480`).
- **O(1) edge ops**: `persist_new_edge` (allocate via [[freelist]] or append, splice at chain head, patch old head's `prev_offset`, update one fixed-width relation line in place); `persist_edge_weight` (8-byte in-place write) (`graph_io.h:68-92`).
- **`update_node_edges`**: full rewrite of a node's relation list + edge chunks (used by delete's inbound cleanup and the overwrite-compaction path).
- **`build_inbound_index`**: O(N+E) scan of live nodes (skips [[tombstoning]] slots) to build the reverse index (`graph_io.h:96-104`).
- Fixed-width `NodeIndex` → O(1) lookup via `seekg(id * sizeof(NodeIndex))`.

## Relations

- **part-of** → [[architecture-overview]]
- **contains** → [[freelist]]
- **contains** → [[logger]]
- **contains** → [[tombstoning]]
- **depends-on** → [[odt-layer]]
- **depends-on** → [[pod-layout]]
- **depends-on** → [[type-registry]]
- **used-by** → [[graph-core]]
- **used-by** → [[in-edges-index]]
- **relates-to** → [[build-and-run]]
- **relates-to** → [[doc-drift-caveats]]
- **documented-in** → docs/modules/db.md

## Sources

- `graph_core/io/graph_io.h`, `graph_core/io/graph_io.cpp`, `graph_core/io/io_utils.{h,cpp}`
