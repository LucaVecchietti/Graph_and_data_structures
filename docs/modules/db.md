# Module: db (on-disk format)

> Binary file layout used by `graph_core/io/` to persist nodes, edges, and graph metadata.

| Campo | Valore |
|---|---|
| Tipo | module |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-26 |
| Commit di riferimento | b6e7304-dirty |
| Mirror | — |

---

## Overview

`db/` is the on-disk store. It is a directory of four flat binary files plus a couple of runtime logs. The format is hand-rolled, packed (`#pragma pack(push, 1)`), little-endian (because the project targets Windows x86-64), and host-byte-order-dependent (no endianness translation is performed).

`DB_PATH` is `"../db"` relative to the running binary (see `graph_core/costants.h`). When run from `build/`, this resolves to the repo root's `db/` folder.

## Struttura

```
db/
├── meta.dat       MetaRecord (truncated + rewritten on every insert)
├── nodes.idx      array of NodeIndex records (appended on insert)
├── nodes.dat      NodeRecord<T> + RelationNodeList (appended on insert)
└── edges.dat      Edge records (appended on insert)
```

There is no header magic, no version field, no checksum. Reading a `db/` produced by a build with a different `NodeType` enum or different POD layout will silently misinterpret data.

## Design

- **Append-only data files** (`nodes.dat`, `nodes.idx`, `edges.dat`) — opened with `std::ios::binary | std::ios::app`. Cheap inserts, no in-place updates.
- **Truncated meta** — `meta.dat` is opened with `std::ios::binary | std::ios::trunc` and fully rewritten on every change. It's tiny (24 bytes) so the cost is irrelevant.
- **Fixed-width `NodeIndex`** — allows O(1) lookup by id via `seekg(id * sizeof(NodeIndex))` in `nodes.idx`.
- **Type tag inside `NodeIndex`** — lets `read_node` dispatch to the right `read_typed_node<T>` without touching `nodes.dat`.
- **Edges stored separately** — `edges.dat` holds the flat list of all edges; each `RelationNodeList` tail entry stores `(name, edge_offset, edge_count)` pointing into it.

See [Append-only data files, truncated meta](../legacy/design_decisions.md#2026-05-26--append-only-data-files-truncated-meta).

## File formats

### `meta.dat`

A single `MetaRecord` POD, 24 bytes:

```
offset  size  field
  0      8    next_id        (uint64_t)
  8      8    node_count     (uint64_t)
 16      8    free_count     (uint64_t, currently unused)
```

Rewritten in full on every insert via `write_meta`.

### `nodes.idx`

A flat array of `NodeIndex` records, 25 bytes each (packed):

```
offset  size  field
  0      8    id              (uint64_t)
  8      8    offset          (uint64_t) → into nodes.dat (NodeRecord<T> or ComplexHeader)
 16      8    relation_offset (uint64_t) → into nodes.dat (RelationNodeList)
 24      1    type_id         (uint8_t)  → NodeType enum  (0..4 primitives, 255 = COMPLEX)
```

Random access by id: `seekg(id * 25)`.

`type_id == 255` (`NodeType::COMPLEX`) marks a record whose payload at `offset` is a `ComplexHeader` followed by two raw strings (`type_label`, `json_attributes`) — see below. The on-disk path for COMPLEX is WIP and no record of this kind is produced by the current code; the value `255` is reserved.

### `nodes.dat`

For each inserted node, the file contains two regions in this order (but not contiguous across nodes — they are interleaved as `write_node` produces them):

1. **Payload** — depends on `NodeIndex.type_id`:
   - **Primitive types** (`INT, FLOAT, DOUBLE, CHAR, BOOL`): a `NodeRecord<T>` — `sizeof(T)` bytes of raw payload. 4, 4, 8, 1, 1 bytes respectively.
   - **`COMPLEX` (255)**: a `ComplexHeader` (16 bytes: `type_label_size` + `json_attributes_size`, both `uint64_t`) followed by `type_label_size` raw bytes of the label, then `json_attributes_size` raw bytes of the JSON. Format reserved but **not yet written** by any current code path — WIP.
2. **`RelationNodeList` header + tail**:
   ```
   offset  size  field
     0      8    type_count            (uint64_t)
   ```
   Then `type_count` entries of:
   ```
   8    name_length              (uint64_t)
   N    name bytes               (raw, no NUL terminator)
   8    edge_offset              (uint64_t) → into edges.dat
   8    edge_count               (uint64_t) → number of consecutive Edge records
   ```

The byte offsets are recorded in the corresponding `NodeIndex` (`offset` → start of `NodeRecord<T>`, `relation_offset` → start of `RelationNodeList` header).

### `edges.dat`

A flat array of `Edge` POD records, 32 bytes each (packed):

```
offset  size  field
  0      8    id          (uint64_t) — per-node-relation index (see BUG-002)
  8      8    weight      (int64_t)
 16      8    to_node     (uint64_t) — destination node id
 24      8    from_node   (uint64_t) — source node id
```

Edges for a given `(node, relation)` are stored consecutively starting at `RelationNodeList.tail.edge_offset` for `edge_count` entries.

## Diagrammi

### Cross-file pointer chain for one node

```
   nodes.idx                          nodes.dat                           edges.dat
   ┌──────────────────┐               ┌─────────────────────────┐         ┌───────────┐
   │ NodeIndex { id=3 │               │ NodeRecord<int> { 42 }  │         │ Edge { } |
   │   offset      ──┼───────────────▶│                         │         │ Edge { } |
   │   relation_off ─┼───────┐        │                         │   ┌────▶│ Edge { } |
   │   type_id=INT   │       │        │ RelationNodeList:       │   │     │ ...      │
   └──────────────────┘      │        │   type_count = 2        │   │     └───────────┘
                             └───────▶│   [4]"road" off ────────┼───┘
                                      │   [5]"train" off ───────┼─────┐
                                      └─────────────────────────┘     │
                                                                      ▼
                                                              (more edges in edges.dat)
```

### Insert ordering inside `write_node`

```
1. open nodes.dat (binary | app)
2. seek end
3. write NodeRecord<T>            ← records "offset" = tellp() before this write
4. write RelationNodeList header + tail  ← records "relation_offset" = tellp() before this write
        (per-relation: edges appended to edges.dat, then [name][off][count] in nodes.dat)
5. open nodes.idx (binary | app)
6. write NodeIndex { id, offset, relation_offset, type_id }
```

## Dipendenze

**IN**: written/read exclusively by `graph_core/io/graph_io.{h,cpp}`.

**OUT**: filesystem only.

## Voci legacy collegate

- [Introduzione tag NodeType::COMPLEX + ComplexRecord (WIP)](../legacy/design_decisions.md#2026-05-26--introduzione-tag-nodetypecomplex--complexrecord-wip)
- [Append-only data files, truncated meta](../legacy/design_decisions.md#2026-05-26--append-only-data-files-truncated-meta)
- [Single-open append su nodes.dat](../legacy/design_decisions.md#2026-05-26--single-open-append-su-nodesdat)
- [POD packed e fragilità ABI](../legacy/design_decisions.md#2026-05-26--pod-packed-e-fragilità-abi)
- [BUG-001 — add_edge non persiste](../legacy/known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco)
- [BUG-002 — edge_idx non globale](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi)

## Riferimenti

- `graph_core/struct/pod_struct.h:16` — `NodeType` enum (incl. `COMPLEX = 255`).
- `graph_core/struct/pod_struct.h:40-126` — POD layouts for `NodeIndex`, `NodeRecord`, `RelationNodeList`, `Edge`, `MetaRecord`, `FreeRecord`.
- `graph_core/struct/pod_struct.h:134` — `ComplexHeader`.
- `graph_core/io/graph_io.h:43-109` — write templates.
- `graph_core/io/graph_io.cpp:13` — `write_node_index`.
- `graph_core/io/graph_io.cpp:49` — `read_node` (dispatch on `type_id` — `COMPLEX` case TBD).
- `graph_core/io/graph_io.cpp:72` — `write_meta` (truncating).
- `graph_core/io/io_utils.cpp:4` — `write_string` (length-prefixed).
