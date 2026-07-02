---
node: odt-layer
title: ODT — Object Data Transfer (Domain↔POD)
type: domain
tags: [odt, translation]
updated: 2026-07-01
---

# ODT — Object Data Transfer

`odt/node_odt.{h,cpp}` + `odt/edge_odt.{h,cpp}`: the only layer allowed to convert between the RAM domain structs and the on-disk PODs.

## Facts

- `node_to_record<T>` — wraps `node.data` in `NodeRecord<T>` with a `static_assert(is_trivially_copyable)` (`node_odt.h:25-32`).
- `complex_node_to_record` — builds a `ComplexHeader` and assigns the sidecar `json_file_path` (recycling a `prog_number`); backs [[complex-nodes]].
- `node_to_relation_list` — builds the [[relation-batch]] header (`type_count`, `free_bytes`, `batch_size`); the tail lines are written by [[persistence-io]].
- `node_to_node_index`, `edge_to_pod` (with optional `prev_offset`/`next_offset`).
- `RelationEntry{name, edge_offset, edge_count}` — POD→domain read helper (`node_odt.h:77-82`).
- **Removed** (BUG-003/004): `reconstruct_neighbors` and `node_form_pod` — never worked; real reconstruction is `read_typed_node` in [[persistence-io]] (`node_odt.h:84-88`). See [[doc-drift-caveats]].

## Relations

- **part-of** → [[architecture-overview]]
- **depends-on** → [[pod-layout]]
- **used-by** → [[persistence-io]]
- **used-by** → [[complex-nodes]]
- **relates-to** → [[doc-drift-caveats]]
- **documented-in** → docs/modules/graph_core.md

## Sources

- `graph_core/odt/node_odt.h`, `graph_core/odt/node_odt.cpp`, `graph_core/odt/edge_odt.{h,cpp}`
