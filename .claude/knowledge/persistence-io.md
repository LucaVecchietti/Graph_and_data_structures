---
node: persistence-io
title: Persistence & I/O (append-only disk format)
type: domain
tags: [io, disk, persistence, bugs]
updated: 2026-07-01
---

# Persistence & I/O

The disk layer under `db/` is append-only for data, with a fully-rewritten meta file.

## Facts

- Append-only writes to `nodes.dat` / `nodes.idx` / `edges.dat`; `meta.dat` is fully rewritten each time.
- Fixed-width `NodeIndex` (25 bytes) gives O(1) lookup via `seekg(id * 25)`.
- **`read_node` leaves neighbor pointers as `nullptr`** — after loading, adjacency neighbors are unlinked; traversers must re-resolve.
- **`reconstruct_neighbors` is a stub** returning an empty map (BUG-003).
- **`node_form_pod` typo** references `node.neihborgs` (BUG-004) — does not compile if instantiated.
- `db/*` contents are gitignored — schema-breaking POD changes should be paired with deleting stale `db/` files.

## Relations

- **part-of** → [[architecture-overview]]
- **used-by** → [[graph-core]]
- **relates-to** → [[build-and-run]]
- **documented-in** → docs/modules/db.md
- **part-of** → [[Index]]

## Sources

- `graph_core/io/graph_io.h`, `graph_core/io/io_utils.*`
- `graph_core/struct/pod_struct.h`, `db/`
