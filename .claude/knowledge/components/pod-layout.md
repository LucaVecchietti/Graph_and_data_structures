---
id: pod-layout
title: On-disk POD records
type: component
tags: [disk-format, structs]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# On-disk POD records
> pod_struct.h: every on-disk record, packed to 1 byte and host-byte-order dependent.

## Responsibility

Define every on-disk record. All of them are `#pragma pack(push,1)`, so the layout is exact,
unpadded, and host-byte-order dependent.

## Where it lives

`graph_core/struct/pod_struct.h`, with the width constants in `graph_core/costants.h`.

## How it behaves

- `NodeIndex {uint64 id, offset, relation_offset; NodeType type_id}` = **25 bytes**, fixed
  width, which is what gives O(1) id lookup by `seekg(id * sizeof(NodeIndex))`.
- `NodeRecord<T> {T data}` - primitives only.
- `NodeRelationList` - the fixed-width relation batch, see [[relation-batch]].
- `Edge` - 48 bytes with `prev_offset`/`next_offset`, see [[edge-record]].
- `MetaRecord` - six counters: `next_id`, `node_count`, `free_count`, `edge_count`,
  `next_edge_id`, `free_edge_count`. `meta.dat` is fully rewritten on every operation.
- `NodeType : uint8_t` - INT=0, FLOAT=1, DOUBLE=2, CHAR=3, BOOL=4, TOMBSTONE=254, COMPLEX=255.
  5..253 are deliberately reserved for future primitives.
- Freelist records: `NodeFreeOffset {idx, offset, size}`, `RelationNodeListFreeOffset
  {offset, size}`, `BatchOfEdgesFreeOffset {idx, offset, size}`.
- `ComplexHeader {type_label_size, json_file_path_size}` and `JsonMeta {prog_number}` back
  [[complex-nodes]].

## Contracts and constraints

- No magic number, no version field, no checksum. A layout change is a silent schema break:
  the stale `db/` must be deleted. `main.cpp` wipes `db/` on every run for exactly this reason.
- Host byte order only - the files are not portable across architectures.

## Links

- part of [[graph-core-architecture]] - the POD layer
- contains [[relation-batch]] - the relation list record
- contains [[edge-record]] - the edge record
- contains [[type-registry]] - the C++ type to NodeType mapping
- used by [[persistence-io]] - reads and writes these records
- used by [[odt-layer]] - produces them from domain structs
- relates to [[complex-nodes]] - ComplexHeader and JsonMeta live here
- relates to [[tombstoning]] - `NodeType::TOMBSTONE` is defined here
- specified by [[decision-pod-vs-domain-split]] - why these exist apart from the domain structs
