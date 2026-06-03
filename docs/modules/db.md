# Module: db (on-disk format)

> Binary file layout used by `graph_core/io/` to persist nodes, edges, and graph metadata.

| Campo | Valore |
|---|---|
| Tipo | module |
| Lingua | en |
| Ultimo aggiornamento | 2026-06-03 |
| Commit di riferimento | ca567e4 |
| Mirror | — |

---

## Overview

`db/` is the on-disk store. It is a directory of four flat binary files plus a couple of runtime logs. The format is hand-rolled, packed (`#pragma pack(push, 1)`), little-endian (because the project targets Windows x86-64), and host-byte-order-dependent (no endianness translation is performed).

`DB_PATH` is `"../db"` relative to the running binary (see `graph_core/costants.h`). When run from `build/`, this resolves to the repo root's `db/` folder.

## Struttura

```
db/
├── meta.dat                              MetaRecord (truncated + rewritten on every insert)
├── nodes.idx                             array of NodeIndex records (appended on insert)
├── nodes.dat                             NodeRecord<T> | ComplexHeader+strings + RelationNodeList (appended on insert)
├── edges.dat                             Edge records (appended on insert)
├── freelist/                             size-segregated free-offset bins (created on first delete)
│   ├── nodes_{size}.dat                  NodeFreeOffset records, one bin per NodeRecord size
│   ├── rel_{size}.dat                    RelationNodeListFreeOffset records, one bin per region size
│   └── edges_{size}.dat                  BatchOfEdgesFreeOffset records, one bin per chunk size
└── attributes/                           COMPLEX sidecars (created on first COMPLEX node)
    ├── attributes_meta.dat               JsonMeta (truncated + rewritten on every COMPLEX write)
    └── {prog_number}_{type_label}.json   JSON attributes payload per COMPLEX node
```

There is no header magic, no version field, no checksum. Reading a `db/` produced by a build with a different `NodeType` enum or different POD layout will silently misinterpret data. The most recent schema break (2026-06-02) added three edge-bookkeeping fields to `MetaRecord`, taking it from 24 to 48 bytes; the prior one (2026-05-30) added `batch_size` to `RelationNodeList` (8 → 16 bytes). Any `db/` produced before the relevant commit must be wiped.

The `attributes/` subtree only exists once at least one COMPLEX node has been written (or once `read_json_attributes_meta` is called and lazy-creates `attributes_meta.dat`). The COMPLEX write+read path compiles and runs end-to-end since 2026-05-30 ([BUG-009..BUG-013](../legacy/known_bugs.md) and [BUG-015](../legacy/known_bugs.md) fixed); the only remaining open item on this path is [BUG-014](../legacy/known_bugs.md) (`prog_number` never persisted, so two COMPLEX nodes with the same `type_label` collide on the same sidecar file). See the [sidecar JSON design decision](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex).

## Design

- **Append-only data files** (`nodes.dat`, `edges.dat`) — opened with `std::ios::binary | std::ios::app`. Cheap inserts, no in-place updates.
- **`nodes.idx`: append-only on insert, in-place patch on edge update** — new entries appended by `write_node_index`; `update_node_edges` patches `NodeIndex.relation_offset` in place (8-byte write at a known offset). Single source of in-place mutation in the system. See [Edge persistence](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch).
- **Truncated meta** — `meta.dat` (and `attributes/attributes_meta.dat`) are opened with `std::ios::binary | std::ios::trunc` and fully rewritten on every change. They're tiny (48 bytes / 8 bytes) so the cost is irrelevant.
- **Fixed-width `NodeIndex`** — allows O(1) lookup by id via `seekg(id * sizeof(NodeIndex))` in `nodes.idx`. Also enables the in-place patch above.
- **Type tag inside `NodeIndex`** — lets `read_node` dispatch to the right `read_typed_node<T>` without touching `nodes.dat`.
- **Edges stored separately** — `edges.dat` holds the flat list of all edges; each `RelationNodeList` tail entry stores `(name, edge_offset, edge_count)` pointing into it.
- **Edge updates: append + obsolete** — `add_edge` rewrites the affected node's `RelationNodeList` and all its edge chunks at fresh offsets, then patches `NodeIndex.relation_offset`. The old regions become orphaned bytes. `update_node_edges` still only **logs** them (it is not yet wired to the freelist), so `add_edge` continues to leak. See [Edge persistence](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch).
- **Freelist: size-segregated bins (since 2026-06-03)** — `db/freelist/<prefix>_<size>.dat`, one bin file per distinct free-region size. Push (`delete_node`) appends a free-offset record; pop (`insert` reuse path) reads the last record and truncates one record — both O(1), and a pop is always an exact fit. `delete_node` populates the bins; `insert` reuses a freed `nodes` slot for fixed-size primitives. `FreeRecord` was removed in favour of three sized POD records (`NodeFreeOffset` / `RelationNodeListFreeOffset` / `BatchOfEdgesFreeOffset`). `MetaRecord.free_count` / `free_edge_count` are still unused (`delete_node` does not update meta — [BUG-016](../legacy/known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex)). See [Freelist a bin segregati](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo).
- **COMPLEX payload stored out-of-line** — for `NodeType::COMPLEX`, the record in `nodes.dat` carries only the header + the labels; the actual JSON attributes live in `attributes/{prog_number}_{type_label}.json`, named via the `JsonMeta.prog_number` counter.

See [Append-only data files, truncated meta](../legacy/design_decisions.md#2026-05-26--append-only-data-files-truncated-meta), [Edge persistence](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch) and [Storage sidecar JSON per nodi COMPLEX](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex).

## File formats

### `meta.dat`

A single `MetaRecord` POD, 48 bytes (24 → 48 on 2026-06-02, edge fields added):

```
offset  size  field
  0      8    next_id          (uint64_t)  — next node id
  8      8    node_count       (uint64_t)
 16      8    free_count       (uint64_t)  — node freelist size, currently unused
 24      8    edge_count       (uint64_t)  — number of live edges
 32      8    next_edge_id     (uint64_t)  — next edge id (source of Edge.id)
 40      8    free_edge_count  (uint64_t)  — edge freelist size, currently unused
```

Rewritten in full on every insert via `write_meta`; also rewritten by `add_edge` whenever a genuinely new edge bumps `edge_count` / `next_edge_id`.

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

`type_id == 255` (`NodeType::COMPLEX`) marks a record whose payload at `offset` is a `ComplexHeader` followed by two length-prefixed strings (`type_label`, `json_file_path`) — see below. The actual JSON payload lives in a separate sidecar file under `attributes/`. Write and read paths for COMPLEX are functional since 2026-05-30 (see [BUG-009..BUG-013](../legacy/known_bugs.md) and [BUG-015](../legacy/known_bugs.md)); [BUG-014](../legacy/known_bugs.md) is still open (sidecar filename collisions for two COMPLEX nodes with the same `type_label`). The value `255` is reserved.

**In-place mutation of `NodeIndex.relation_offset`.** Since 2026-05-30, `add_edge` causes the node's `relation_offset` to be patched in place by `update_node_edges` (the rest of the entry stays untouched: `id`, `offset`, and `type_id` are still set once at insert time and never moved). This is the single in-place write in the system; the file is opened with `std::ios::binary | std::ios::in | std::ios::out` (NOT `app` — Windows ignores `seekp` in app mode) and the patch lands at `node_id * sizeof(NodeIndex) + offsetof(NodeIndex, relation_offset)`. See [Edge persistence](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch).

### `nodes.dat`

For each inserted node, the file contains two regions in this order (but not contiguous across nodes — they are interleaved as `write_node` produces them):

1. **Payload** — depends on `NodeIndex.type_id`:
   - **Primitive types** (`INT, FLOAT, DOUBLE, CHAR, BOOL`): a `NodeRecord<T>` — `sizeof(T)` bytes of raw payload. 4, 4, 8, 1, 1 bytes respectively.
   - **`COMPLEX` (255)**: a `ComplexHeader` (16 bytes: `type_label_size` + `json_file_path_size`, both `uint64_t`) followed by two length-prefixed strings written via `write_string` — `type_label` then `json_file_path`. The JSON attributes themselves are **not** stored in `nodes.dat`: they live in the sidecar file at `attributes/{json_file_path}` (path relative to `JSON_ATTR_PATH`). Functional since 2026-05-30.
2. **`RelationNodeList` header + tail**:
   ```
   offset  size  field
     0      8    type_count            (uint64_t)
     8      8    batch_size            (uint64_t) → byte size of the tail that follows
   ```
   Then `type_count` entries of:
   ```
   8    name_length              (uint64_t)
   N    name bytes               (raw, no NUL terminator)
   8    edge_offset              (uint64_t) → into edges.dat
   8    edge_count               (uint64_t) → number of consecutive Edge records
   ```
   The total size of the tail equals `batch_size` and the total on-disk size of the relation list region equals `sizeof(RelationNodeList) + batch_size = 16 + batch_size`.

The byte offsets are recorded in the corresponding `NodeIndex` (`offset` → start of `NodeRecord<T>` or `ComplexHeader`, `relation_offset` → start of `RelationNodeList` header). When `add_edge` runs on an existing node, a new `RelationNodeList` region is appended at end-of-file and `NodeIndex.relation_offset` is patched in place to point to it — the previous region becomes orphaned bytes inside `nodes.dat` (no freelist persistence yet, see [Edge persistence](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch)).

### `edges.dat`

A flat array of `Edge` POD records, 32 bytes each (packed):

```
offset  size  field
  0      8    id          (uint64_t) — globally-unique edge id from MetaRecord.next_edge_id
  8      8    weight      (int64_t)
 16      8    to_node     (uint64_t) — destination node id
 24      8    from_node   (uint64_t) — source node id
```

Edges for a given `(node, relation)` are stored consecutively starting at `RelationNodeList.tail.edge_offset` for `edge_count` entries. When `add_edge` modifies a `(node, relation)`, a fresh contiguous chunk is appended at end-of-file and the new entry in the rewritten `RelationNodeList` points there — the old chunk becomes orphaned, so `edges.dat` grows monotonically with every `add_edge`. Since 2026-06-02 each `Edge.id` is globally unique and stable: it is assigned once from `MetaRecord.next_edge_id` and preserved across the full-node rewrites (an edge read back from disk keeps its id, so re-saving it does not change it). See [BUG-002 fixed](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi).

### `freelist/<prefix>_<size>.dat`

Size-segregated free-offset bins, created lazily on the first `delete_node` (via `write_free_offset`, which `create_directories` on `db/freelist/`). One file per `(prefix, size)` pair; every record in a given file is the same fixed-size POD, so the file is a flat array. Bins are LIFO: push = append one record, pop = read the last record + `resize_file` down by one record.

| Prefix | Record POD | Region reclaimed |
|---|---|---|
| `nodes` | `NodeFreeOffset` (24 B: `idx`, `offset`, `size`) | a freed `NodeRecord` region in `nodes.dat` + its reusable id slot in `nodes.idx`. `size ∈ {1,4,8}` for the current primitives. |
| `rel` | `RelationNodeListFreeOffset` (16 B: `offset`, `size`) | a freed `RelationNodeList` region (header + tail) in `nodes.dat`. |
| `edges` | `BatchOfEdgesFreeOffset` (24 B: `idx`, `offset`, `size`) | a freed contiguous chunk of `Edge` records in `edges.dat` (`size = edge_count * 32`). |

The `<size>` in the filename is the byte size of the free region; it makes a pop an exact fit with no scan. Only `delete_node` writes these bins today; `insert` reads only the `nodes` bins (primitives). `update_node_edges`' orphaned regions are **not** yet pushed here. See the [freelist design decision](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo).

### `attributes/attributes_meta.dat`

A single `JsonMeta` POD, 8 bytes:

```
offset  size  field
  0      8    prog_number   (uint64_t) — monotonic counter for unique sidecar names
```

Truncated and rewritten in full via `write_json_attributes_meta`. Lazy-created on first call to `read_json_attributes_meta` (with `prog_number = 0`).

### `attributes/{prog_number}_{type_label}.json`

The actual JSON attributes of a COMPLEX node, written as raw UTF-8 text (no length prefix, no framing). Filename composition:

- `prog_number` comes from `JsonMeta.prog_number` at write time.
- `type_label` is the COMPLEX node's runtime label (e.g. `Athlete`, `Item`).

The exact filename **must** match the `json_file_path` string stored in the on-disk `ComplexHeader` so that the read path can reopen the file. Since 2026-05-30 (fix of [BUG-013](../legacy/known_bugs.md)) the same `json_file_path` string is threaded through `complex_node_to_record` → `write_complex` → both `write_string` on `nodes.dat` and the sidecar `std::ofstream` open, so the two sides cannot diverge anymore.

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
3. dispatch on node_type_of_v<T>:
   - primitive T: write NodeRecord<T>             ← "offset" = tellp() before this write
   - COMPLEX:    write ComplexHeader + 2 strings ← "offset" = tellp() before this write
                 and write the sidecar JSON file under attributes/
4. write RelationNodeList POD (type_count + batch_size = 0 for a fresh node)
        and its variable-width tail (empty for a fresh node)
   ← "relation_offset" = tellp() before this write
   (per-relation: edges appended to edges.dat, then [name][off][count] in nodes.dat)
5. open nodes.idx (binary | app)
6. write NodeIndex { id, offset, relation_offset, type_id }
```

### Update ordering inside `update_node_edges` (since 2026-05-30)

```
1. read nodes.idx[node_id]                     ← old NodeIndex
2. read nodes.dat at old relation_offset       ← old RelationNodeList + tail
3. log the orphaned regions                    ← placeholder for the freelist (BUG-001 follow-up)
4. open nodes.dat (binary | app), seek end
5. write the NEW RelationNodeList POD with current batch_size
6. for each relation in node.neighborgs:
     - open edges.dat (binary | app), seek end
     - append the relation's full edge chunk    ← captures new edge_offset
     - append [name][new edge_offset][count] to nodes.dat
7. open nodes.idx (binary | in | out)          ← NOT app: needs in-place seekp
8. seekp(node_id*sizeof(NodeIndex) + offsetof(NodeIndex, relation_offset))
9. write the NEW relation_offset (8 bytes, in place)
```

## Dipendenze

**IN**: written/read exclusively by `graph_core/io/graph_io.{h,cpp}`.

**OUT**: filesystem only.

## Voci legacy collegate

- [Freelist a bin segregati per dimensione esatta + cancellazione nodo](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo)
- [API — FreeRecord rimossa, tre POD free-offset](../legacy/api_changes.md#2026-06-03--freerecord-rimossa-sostituita-da-tre-pod-free-offset)
- [BUG-016 — delete_node prototipo incompleto](../legacy/known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex)
- [Edge persistence: append + obsolete + in-place index patch](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch)
- [Storage sidecar JSON per nodi COMPLEX](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex)
- [Introduzione tag NodeType::COMPLEX + ComplexRecord (WIP)](../legacy/design_decisions.md#2026-05-26--introduzione-tag-nodetypecomplex--complexrecord-wip)
- [Append-only data files, truncated meta](../legacy/design_decisions.md#2026-05-26--append-only-data-files-truncated-meta)
- [Single-open append su nodes.dat](../legacy/design_decisions.md#2026-05-26--single-open-append-su-nodesdat)
- [POD packed e fragilità ABI](../legacy/design_decisions.md#2026-05-26--pod-packed-e-fragilità-abi)
- [API — RelationNodeList con batch_size](../legacy/api_changes.md#2026-05-30--relationnodelist-aggiunto-il-campo-batch_size)
- [API — Graph::add_edge persiste](../legacy/api_changes.md#2026-05-30--graphadd_edge-persistenza-su-disco-via-update_node_edges)
- [API — MetaRecord campi edge](../legacy/api_changes.md#2026-06-02--metarecord-aggiunti-i-campi-edge_count-next_edge_id-free_edge_count)
- [API — neighborgs: EdgeRef](../legacy/api_changes.md#2026-06-02--basenodeneighborgs-da-pairint-basenode-a-edgeref)
- [Decisione — id arco in EdgeRef](../legacy/design_decisions.md#2026-06-02--id-arco-globale-sorgente-in-metarecordnext_edge_id-memorizzato-in-edgeref)
- [API — ComplexHeader rinominato](../legacy/api_changes.md#2026-05-26--complexheaderjson_attributes_size--json_file_path_size)
- [BUG-001 — add_edge non persiste (fixed)](../legacy/known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco)
- [BUG-002 — Edge.id non globale (fixed)](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi)
- [BUG-014 — prog_number non incrementato](../legacy/known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex)

## Riferimenti

- `graph_core/struct/pod_struct.h:16` — `NodeType` enum (incl. `COMPLEX = 255`).
- `graph_core/struct/pod_struct.h:40-220` — POD layouts for `NodeIndex`, `NodeRecord`, `RelationNodeList`, `Edge`, `MetaRecord`, `ComplexHeader`, `JsonMeta`, and the freelist `NodeFreeOffset` / `RelationNodeListFreeOffset` / `BatchOfEdgesFreeOffset` (`FreeRecord` removed 2026-06-03).
- `graph_core/struct/pod_struct.h:136` — `MetaRecord` (now 48 bytes: 3 node + 3 edge counters).
- `graph_core/struct/domain_struct.h:24` — `EdgeRef { id, weight, neighbor }` (RAM-side edge, source of `Edge.id`).
- `graph_core/graph.cpp:57` — `add_edge` (assigns edge id, bumps `next_edge_id`/`edge_count`).
- `graph_core/struct/pod_struct.h:78` — `RelationNodeList` (now 16 bytes: `type_count` + `batch_size`).
- `graph_core/struct/pod_struct.h:134` — `ComplexHeader` (field renamed to `json_file_path_size`).
- `graph_core/struct/pod_struct.h:148` — `JsonMeta`.
- `graph_core/costants.h:9-11` — `META_FILE_PATH`, `JSON_ATTR_META_PATH`, `JSON_ATTR_PATH`.
- `graph_core/io/graph_io.h:24,38,62,45-46` — `write_complex`, `read_complex`, `update_node_edges`, `write_json_attributes_meta`, `read_json_attributes_meta` declarations.
- `graph_core/io/graph_io.h:107` — `write_node` template (`if constexpr` dispatch on `NodeType`).
- `graph_core/io/graph_io.cpp:14` — `write_node_index`.
- `graph_core/io/graph_io.cpp:34` — `write_complex`.
- `graph_core/io/graph_io.cpp:65` — `read_complex` (reads ComplexHeader + 2 strings + sidecar JSON; throws if sidecar missing).
- `graph_core/io/graph_io.cpp:120` — `read_node` (dispatch on `type_id`).
- `graph_core/io/graph_io.cpp:144` — `write_meta` (truncating).
- `graph_core/io/graph_io.cpp:172,190` — `write_json_attributes_meta`, `read_json_attributes_meta`.
- `graph_core/io/graph_io.cpp:247` — `update_node_edges` (rewrites RelationNodeList + edge chunks at fresh offsets, patches `NodeIndex.relation_offset` in place).
- `graph_core/odt/node_odt.cpp:27` — `node_to_relation_list` (now computes `batch_size`).
- `graph_core/io/io_utils.cpp:4` — `write_string` (length-prefixed).
- `graph_core/struct/pod_struct.h:182,196,209` — `NodeFreeOffset`, `RelationNodeListFreeOffset`, `BatchOfEdgesFreeOffset` (freelist bin records).
- `graph_core/io/graph_io.h:321,333,355` — `freelist_bin_path`, `write_free_offset`, `pop_free_offset` (bin path + push/pop).
- `graph_core/io/graph_io.cpp:380` — `delete_node_from_disk` (pushes the node's regions onto the bins).
