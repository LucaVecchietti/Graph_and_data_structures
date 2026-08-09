---
id: odt-layer
title: ODT - object data transfer layer
type: component
tags: [odt, translation]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# ODT - object data transfer layer
> The only layer allowed to convert between RAM domain structs and on-disk PODs.

## Responsibility

Object Data Transfer: the only layer permitted to convert between the RAM domain structs
and the on-disk PODs. Keeping the conversion in one place is what lets the two
representations diverge safely.

## Where it lives

`graph_core/odt/node_odt.{h,cpp}` and `graph_core/odt/edge_odt.{h,cpp}`.

## How it behaves

- `node_to_record<T>` wraps `node.data` into a `NodeRecord<T>`, guarded by
  `static_assert(is_trivially_copyable_v<T>)`.
- `complex_node_to_record` builds a `ComplexHeader` and assigns the sidecar
  `json_file_path`, reusing a recycled `prog_number` when one is available ([[complex-nodes]]).
- `node_to_relation_list` builds the [[relation-batch]] header (`type_count`, `free_bytes`,
  `batch_size`); the fixed-width tail lines are written by [[persistence-io]].
- `node_to_node_index` and `edge_to_pod` (the latter takes optional `prev_offset`/`next_offset`).
- `RelationEntry {name, edge_offset, edge_count}` is the POD-to-domain read helper.

## Contracts and constraints

- `ComplexRecord` is not trivially copyable, so it can never go through `node_to_record`;
  the COMPLEX path is selected with `if constexpr` upstream.
- The older `reconstruct_neighbors` and `node_form_pod` helpers were **removed**, not fixed:
  they had no access to `edges.dat` and could not have worked. Real reconstruction is
  `read_typed_node` in [[persistence-io]]. `CLAUDE.md` still lists them - see [[docs-drift-vs-code]].

## Links

- part of [[graph-core-architecture]] - the translation layer
- depends on [[pod-layout]] - produces and consumes these records
- used by [[persistence-io]] - the only caller on the write path
- relates to [[complex-nodes]] - `complex_node_to_record` is its entry point
- relates to [[docs-drift-vs-code]] - two documented helpers no longer exist
