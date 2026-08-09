---
id: decision-json-sidecar-for-complex
title: Store COMPLEX attributes in JSON sidecar files
type: decision
tags: [complex, disk]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Store COMPLEX attributes in JSON sidecar files
> Keep variable-length JSON out of nodes.dat, in per-node files under db/attributes/.

## Context

Introducing `NodeType::COMPLEX` left one question open: where do a COMPLEX node's JSON
attributes live? Inline as a length-prefixed blob in `nodes.dat`, or in a separate file?
Attributes can run to kilobytes and are read rarely, while `nodes.dat` is the primary file
whose predictable record lengths make seeking cheap.

## Options considered

- **JSON inline in `nodes.dat`** - inflates the primary file with highly variable-length data,
  hurts seek locality for plain `NodeRecord` reads, and makes updating the JSON a file-tail rewrite.
- **A single flat `attributes.dat` with offset and length** - solves fragmentation but
  reintroduces a freelist problem as soon as a JSON payload changes size. Deferred.
- **All JSON files directly in `db/`** - rejected; a subdirectory keeps sidecars separate from
  the primary format files and can be cleaned independently.

## Decision

Move the attributes into per-node sidecar files under `db/attributes/`. `nodes.dat` keeps only
`ComplexHeader {type_label_size, json_file_path_size}` plus the two raw blobs `type_label` and
`json_file_path`. Filenames are `{prog_number}_{type_label}.json`, with `prog_number` sourced
from a `JsonMeta` POD persisted in `db/attributes/attributes_meta.dat`.

A later refinement zero-pads `prog_number` to `COMPLEX_PROG_DIGITS = 20`, which removes the
incidental size variance from the counter's digit count: a record's on-disk size then depends
only on the `type_label` length, so the existing exact-size `complex_<size>` bins act
automatically as **per-type size classes**. Freed sidecars are deleted and their numbers
recycled through `db/freelist/json_prog.dat`.

## Consequences

- A COMPLEX node's on-disk form is no longer contained in `nodes.dat` alone: reconstructing it
  requires a second read, and a missing sidecar is treated as database corruption.
- Sidecar filenames carry ~19 bytes of zero padding - the waste moves into the filename
  instead of dead bytes inside `nodes.dat`, and the bins stay exact.
- Two different types with the same label length share a bin. That is correct and intended.
- With 20 digits, overflowing a `uint64` counter is impossible, so no guard is needed.

## Links

- specifies [[complex-nodes]] - the mechanism this decision defines
- depends on [[freelist]] - per-type size classes and the json_prog recycling stack
- relates to [[db-path-relative-to-cwd]] - `JSON_ATTR_PATH` is relative in the same way
