---
id: complex-nodes
title: COMPLEX nodes and JSON sidecars
type: concept
tags: [complex, json]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# COMPLEX nodes and JSON sidecars
> NodeType::COMPLEX carries a runtime type_label plus JSON attributes stored in a sidecar file.

## What it is

`NodeType::COMPLEX = 255` - a runtime-typed node, closer to a classical database row: it
carries a `type_label` (e.g. `"Athlete"`) plus a set of attributes held as JSON in a sidecar
file. Implemented and exercised by [[smoke-test]] phases 1 and 4.

## How it works

- In RAM: `ComplexRecord {std::string type_label; std::string json_attributes}`. It is **not**
  trivially copyable, so it cannot go through `NodeRecord<T>`/`write_pod`; `insert` and
  `write_node` select the COMPLEX path with `if constexpr`.
- In `nodes.dat`: `ComplexHeader {type_label_size, json_file_path_size}` followed by the
  length-prefixed `type_label` and the length-prefixed `json_file_path`.
- The attributes themselves live in `db/attributes/<20-digit prog>_<type_label>.json`.
  `JsonMeta.prog_number` (persisted in `db/attributes/attributes_meta.dat`) generates the
  unique numbers, and freed numbers are recycled through the `json_prog` [[freelist]] stack.
- `write_complex`/`read_complex` handle header, strings and sidecar. A COMPLEX node whose
  sidecar is missing is treated as a corrupted database and throws.
- The zero-padded prog width (`COMPLEX_PROG_DIGITS = 20`) makes a record's on-disk size
  depend **only** on `type_label` length, so the `complex_<size>` bins behave as per-type
  size classes and give exact-fit reuse of same-type nodes.

## Why it matters here

It is the escape hatch from the closed set of primitive payloads, and the design the roadmap
plans to mirror for edge attribute payloads.

## Links

- part of [[pod-layout]] - ComplexHeader and JsonMeta are POD records
- depends on [[odt-layer]] - `complex_node_to_record` builds the header and sidecar path
- depends on [[freelist]] - per-type size-class bins plus the json_prog stack
- specified by [[decision-json-sidecar-for-complex]] - why the JSON is out of line
- relates to [[type-registry]] - the one non-trivially-copyable registered payload
