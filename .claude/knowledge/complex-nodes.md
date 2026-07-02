---
node: complex-nodes
title: COMPLEX Nodes (typed records + JSON sidecar)
type: concept
tags: [complex, json, sidecar, wip]
updated: 2026-07-01
---

# COMPLEX Nodes

`NodeType::COMPLEX = 255` — a runtime-typed record (like a classical DB row) whose attributes live as JSON in a sidecar file. Now IMPLEMENTED (exercised by [[smoke-test]] phases 1 & 4); some edge cases remain WIP.

## Facts

- RAM side: `ComplexRecord{ std::string type_label; std::string json_attributes }` — **not POD**, so it cannot use `NodeRecord<T>`/`write_pod` (`domain_struct.h:64-68`).
- On disk (in `nodes.dat`): `ComplexHeader{type_label_size, json_file_path_size}` + length-prefixed `type_label` + length-prefixed `json_file_path`. The JSON attributes go to a **sidecar file** under `JSON_ATTR_PATH = ../db/attributes/`, named `"<20-digit prog>_<type_label>.json"` (`graph_io.h:29-56`, `costants.h:26-32`).
- `write_complex`/`read_complex` handle header + strings + sidecar; a COMPLEX node missing its sidecar is treated as a corrupted DB (throws).
- `JsonMeta.prog_number` generates unique filenames; recycled via the `json_prog` [[freelist]] stack.
- The zero-padded prog width (`COMPLEX_PROG_DIGITS = 20`) makes a record's on-disk size depend ONLY on `type_label` length → the `complex_<size>` bins act as **per-type size classes** (exact-fit reuse of same-type nodes).
- `insert`/`delete` dispatch COMPLEX vs primitive via `if constexpr`; the reuse path uses `complex_record_on_disk_size` (`graph_io.h:511-515`).

## Relations

- **part-of** → [[pod-layout]]
- **depends-on** → [[odt-layer]]
- **relates-to** → [[freelist]]
- **relates-to** → [[tombstoning]]
- **relates-to** → [[type-registry]]
- **documented-in** → docs/modules/db.md

## Sources

- `graph_core/struct/pod_struct.h:166-191`, `graph_core/struct/domain_struct.h:56-68`, `graph_core/io/graph_io.{h,cpp}`, `db/attributes/`
