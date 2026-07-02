---
node: pod-layout
title: On-Disk POD Records
type: domain
tags: [pod, disk-format, structs]
updated: 2026-07-01
---

# On-Disk POD Records

`struct/pod_struct.h` — every record is `#pragma pack(push,1)` (no padding), host-byte-order-dependent.

## Facts

- **`NodeIndex`** `{uint64 id, offset, relation_offset; NodeType type_id}` = 25 bytes, fixed width → O(1) id lookup via `seekg(id * sizeof(NodeIndex))` (`pod_struct.h:47-55`).
- **`NodeRecord<T>`** `{T data}` — primitives only (must be trivially copyable) (`pod_struct.h:73-79`).
- **`MetaRecord`** — six counters: `next_id`, `node_count`, `free_count`, `edge_count`, `next_edge_id`, `free_edge_count`; `meta.dat` is fully rewritten each op (`pod_struct.h:151-164`).
- **`NodeType : uint8_t`** — INT=0, FLOAT=1, DOUBLE=2, CHAR=3, BOOL=4, TOMBSTONE=254, COMPLEX=255 (`pod_struct.h:16-38`).
- **Freelist PODs**: `NodeFreeOffset{idx,offset,size}`, `RelationNodeListFreeOffset{offset,size}`, `BatchOfEdgesFreeOffset{idx,offset,size}` (`pod_struct.h:197-232`).
- **`ComplexHeader{type_label_size, json_file_path_size}`** and **`JsonMeta{prog_number}`** back [[complex-nodes]] (`pod_struct.h:171-191`).

## Relations

- **part-of** → [[architecture-overview]]
- **contains** → [[relation-batch]]
- **contains** → [[edge-record]]
- **contains** → [[complex-nodes]]
- **contains** → [[type-registry]]
- **used-by** → [[persistence-io]]
- **used-by** → [[odt-layer]]
- **used-by** → [[graph-core]]
- **used-by** → [[freelist]]
- **relates-to** → [[tombstoning]]
- **documented-in** → docs/modules/db.md

## Sources

- `graph_core/struct/pod_struct.h`
