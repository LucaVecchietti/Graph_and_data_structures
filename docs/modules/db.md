# Module: db (on-disk format)

> Binary file layout used by `graph_core/io/` to persist nodes, edges, and graph metadata.

| Campo | Valore |
|---|---|
| Tipo | module |
| Lingua | en |
| Ultimo aggiornamento | 2026-06-13 |
| Commit di riferimento | cb9939c |
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
│   ├── nodes_{size}.dat                  NodeFreeOffset records, one bin per primitive NodeRecord size
│   ├── complex_{size}.dat                NodeFreeOffset records, one bin per COMPLEX size class (per-type)
│   ├── rel_{size}.dat                    RelationNodeListFreeOffset records, one bin per region size
│   ├── edges_{size}.dat                  BatchOfEdgesFreeOffset records, one bin per chunk size
│   └── json_prog.dat                     LIFO stack of freed prog_numbers (uint64) for COMPLEX sidecars
└── attributes/                           COMPLEX sidecars (created on first COMPLEX node)
    ├── attributes_meta.dat               JsonMeta (truncated + rewritten on every COMPLEX write)
    └── {prog_number:020}_{type_label}.json   JSON attributes payload per COMPLEX node (prog zero-padded)
```

There is no header magic, no version field, no checksum. Reading a `db/` produced by a build with a different `NodeType` enum or different POD layout will silently misinterpret data. The most recent schema break (2026-06-02) added three edge-bookkeeping fields to `MetaRecord`, taking it from 24 to 48 bytes; the prior one (2026-05-30) added `batch_size` to `RelationNodeList` (8 → 16 bytes). Any `db/` produced before the relevant commit must be wiped.

The `attributes/` subtree only exists once at least one COMPLEX node has been written (or once `read_json_attributes_meta` is called and lazy-creates `attributes_meta.dat`). The full COMPLEX lifecycle (write, read, delete, slot reuse) runs end-to-end as of 2026-06-07: [BUG-014](../legacy/known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex) (`prog_number` now incremented + persisted, so sidecars no longer collide) and the COMPLEX part of [BUG-016](../legacy/known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex) are fixed. See the [sidecar JSON design decision](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex) and the [per-type binning decision](../legacy/design_decisions.md#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato).

## Design

- **Append-only data files** (`nodes.dat`, `edges.dat`) — opened with `std::ios::binary | std::ios::app`. Cheap inserts, no in-place updates.
- **`nodes.idx`: append-only on insert, in-place patch on edge update** — new entries appended by `write_node_index`; `update_node_edges` patches `NodeIndex.relation_offset` in place (8-byte write at a known offset). Single source of in-place mutation in the system. See [Edge persistence](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch).
- **Truncated meta** — `meta.dat` (and `attributes/attributes_meta.dat`) are opened with `std::ios::binary | std::ios::trunc` and fully rewritten on every change. They're tiny (48 bytes / 8 bytes) so the cost is irrelevant.
- **Fixed-width `NodeIndex`** — allows O(1) lookup by id via `seekg(id * sizeof(NodeIndex))` in `nodes.idx`. Also enables the in-place patch above.
- **Type tag inside `NodeIndex`** — lets `read_node` dispatch to the right `read_typed_node<T>` without touching `nodes.dat`.
- **Edges stored separately** — `edges.dat` holds the flat list of all edges; each `RelationNodeList` tail entry stores `(name, edge_offset, edge_count)` pointing into it.
- **Edge updates: append + obsolete + reclaim + reuse** — `add_edge` rewrites the affected node's `RelationNodeList` and all its edge chunks, then patches `NodeIndex.relation_offset`. Since 2026-06-07 ([BUG-017](../legacy/known_bugs.md#2026-06-07--bug-017-update_node_edges-orfanizza-regioni-senza-spingerle-sulla-freelist)) `update_node_edges` pushes the old `RelationNodeList` onto the `rel` bin and each old edge chunk onto the `edges` bin, zeroes the bytes, and bumps `free_edge_count`. Since 2026-06-13 it also **reuses** those bins: the new relation-list and edge chunks are written **pop-then-append** — an exact-size freed region (`rel` / `edges`) is reclaimed in place if available, else appended at EOF. Because step 2 just pushed the old regions, a weight-overwrite pops back the same-size holes (LIFO size-segregated) → in-place overwrite, no file growth. Only the `edges` pop decrements `free_edge_count`. See [Reuse of the rel/edges freelist bins](../legacy/design_decisions.md#2026-06-13--reuse-of-the-reledges-freelist-bins-edge-space-compaction) and [Edge persistence](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch).
- **Freelist: size-segregated bins (since 2026-06-03)** — `db/freelist/<prefix>_<size>.dat`, one bin file per distinct free-region size. Push (`delete_node`) appends a free-offset record; pop (`insert` reuse path) reads the last record and truncates one record — both O(1), and a pop is always an exact fit. `delete_node` populates the bins; `insert` reuses a freed slot for primitives (`nodes` bins) and for COMPLEX (`complex` bins); `update_node_edges` reuses the `rel`/`edges` bins on edge rewrite (since 2026-06-13). `FreeRecord` was removed in favour of three sized POD records (`NodeFreeOffset` / `RelationNodeListFreeOffset` / `BatchOfEdgesFreeOffset`). Since 2026-06-07 `delete_node` updates `MetaRecord.node_count`/`free_count`/`free_edge_count` ([BUG-016](../legacy/known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex) closed); `update_node_edges` bumps `free_edge_count` by the number of orphaned chunks on push and decrements it on each `edges`-bin reuse, so it round-trips on a weight-overwrite. See [Freelist a bin segregati](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo) and [Reuse of the rel/edges freelist bins](../legacy/design_decisions.md#2026-06-13--reuse-of-the-reledges-freelist-bins-edge-space-compaction).
- **COMPLEX payload stored out-of-line, per-type size class** — for `NodeType::COMPLEX`, the record in `nodes.dat` carries only the header + the two labels; the JSON attributes live in `attributes/{prog_number:020}_{type_label}.json`. The `prog_number` is **zero-padded to `COMPLEX_PROG_DIGITS` (20)**, which makes the record's on-disk size a pure function of `type_label` length — so the exact-size `complex_<size>` bins act as per-type size classes. On COMPLEX delete the sidecar file is removed and its `prog_number` recycled onto `freelist/json_prog.dat`. See the [per-type binning decision](../legacy/design_decisions.md#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato).

See [Append-only data files, truncated meta](../legacy/design_decisions.md#2026-05-26--append-only-data-files-truncated-meta), [Edge persistence](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch) and [Storage sidecar JSON per nodi COMPLEX](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex).

## File formats

### `meta.dat`

A single `MetaRecord` POD, 48 bytes (24 → 48 on 2026-06-02, edge fields added):

```
offset  size  field
  0      8    next_id          (uint64_t)  — next node id
  8      8    node_count       (uint64_t)
 16      8    free_count       (uint64_t)  — free node slots across all bins (+1 delete, −1 reuse)
 24      8    edge_count       (uint64_t)  — number of live edges
 32      8    next_edge_id     (uint64_t)  — next edge id (source of Edge.id)
 40      8    free_edge_count  (uint64_t)  — freed edge chunks (delete + update_node_edges)
```

Rewritten in full on every insert via `write_meta`; also rewritten by `add_edge` whenever a genuinely new edge bumps `edge_count` / `next_edge_id`.

### `nodes.idx`

A flat array of `NodeIndex` records, 25 bytes each (packed):

```
offset  size  field
  0      8    id              (uint64_t)
  8      8    offset          (uint64_t) → into nodes.dat (NodeRecord<T> or ComplexHeader)
 16      8    relation_offset (uint64_t) → into nodes.dat (RelationNodeList)
 24      1    type_id         (uint8_t)  → NodeType enum  (0..4 primitives, 254 = TOMBSTONE, 255 = COMPLEX)
```

Random access by id: `seekg(id * 25)`.

`type_id == 255` (`NodeType::COMPLEX`) marks a record whose payload at `offset` is a `ComplexHeader` followed by two length-prefixed strings (`type_label`, `json_file_path`) — see below. The actual JSON payload lives in a separate sidecar file under `attributes/`. The full COMPLEX path (write, read, delete, reuse) runs end-to-end as of 2026-06-07 ([BUG-014](../legacy/known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex) fixed — no more sidecar collisions). `type_id == 254` (`NodeType::TOMBSTONE`, added 2026-06-07) marks a logically-deleted slot: the entry stays (id reusable via the freelist) but `offset`/`relation_offset` are zeroed and the referenced bytes have been zero-filled; `read_node` on a tombstoned slot throws. The values `254` and `255` are reserved.

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

The byte offsets are recorded in the corresponding `NodeIndex` (`offset` → start of `NodeRecord<T>` or `ComplexHeader`, `relation_offset` → start of `RelationNodeList` header). When `add_edge` runs on an existing node, the previous `RelationNodeList` region is pushed onto the `rel` freelist bin and zeroed; the new region is then written **pop-then-append** — reusing an exact-size freed `rel` hole in place if one exists, else appended at end-of-file — and `NodeIndex.relation_offset` is patched in place to point to it (since 2026-06-07 for the push, [BUG-017](../legacy/known_bugs.md#2026-06-07--bug-017-update_node_edges-orfanizza-regioni-senza-spingerle-sulla-freelist); since 2026-06-13 for the reuse, see [Reuse of the rel/edges freelist bins](../legacy/design_decisions.md#2026-06-13--reuse-of-the-reledges-freelist-bins-edge-space-compaction) and [Edge persistence](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch)).

### `edges.dat`

A flat array of `Edge` POD records, 32 bytes each (packed):

```
offset  size  field
  0      8    id          (uint64_t) — globally-unique edge id from MetaRecord.next_edge_id
  8      8    weight      (int64_t)
 16      8    to_node     (uint64_t) — destination node id
 24      8    from_node   (uint64_t) — source node id
```

Edges for a given `(node, relation)` are stored consecutively starting at `RelationNodeList.tail.edge_offset` for `edge_count` entries. When `add_edge` modifies a `(node, relation)`, the old chunk is pushed onto the `edges` freelist bin and zeroed (since 2026-06-07, [BUG-017](../legacy/known_bugs.md#2026-06-07--bug-017-update_node_edges-orfanizza-regioni-senza-spingerle-sulla-freelist)); the new chunk is then written **pop-then-append** — reusing an exact-size freed `edges` hole in place if one exists, else appended at end-of-file — and the new entry in the rewritten `RelationNodeList` points there (since 2026-06-13, [Reuse of the rel/edges freelist bins](../legacy/design_decisions.md#2026-06-13--reuse-of-the-reledges-freelist-bins-edge-space-compaction)). On a weight-overwrite the chunk size is unchanged, so the just-freed hole is reclaimed in place and `edges.dat` does not grow; the **edge ids are not recycled** (each `Edge` keeps its own id, the popped chunk's starting id is ignored). Since 2026-06-02 each `Edge.id` is globally unique and stable: it is assigned once from `MetaRecord.next_edge_id` and preserved across the full-node rewrites (an edge read back from disk keeps its id, so re-saving it does not change it). See [BUG-002 fixed](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi).

### `freelist/<prefix>_<size>.dat`

Size-segregated free-offset bins, created lazily on the first `delete_node` (via `write_free_offset`, which `create_directories` on `db/freelist/`). One file per `(prefix, size)` pair; every record in a given file is the same fixed-size POD, so the file is a flat array. Bins are LIFO: push = append one record, pop = read the last record + `resize_file` down by one record.

| Prefix | Record POD | Region reclaimed |
|---|---|---|
| `nodes` | `NodeFreeOffset` (24 B: `idx`, `offset`, `size`) | a freed primitive `NodeRecord` region in `nodes.dat` + its reusable id slot in `nodes.idx`. `size ∈ {1,4,8}` for the current primitives. |
| `complex` | `NodeFreeOffset` (24 B: `idx`, `offset`, `size`) | a freed COMPLEX record region in `nodes.dat` + its reusable id slot. `size = (22 + COMPLEX_PROG_DIGITS) + 2·L` — constant per `type_label` length, so the bin is a per-type size class. |
| `rel` | `RelationNodeListFreeOffset` (16 B: `offset`, `size`) | a freed `RelationNodeList` region (header + tail) in `nodes.dat`. |
| `edges` | `BatchOfEdgesFreeOffset` (24 B: `idx`, `offset`, `size`) | a freed contiguous chunk of `Edge` records in `edges.dat` (`size = edge_count * 32`). |

`json_prog.dat` (no `<size>` suffix) is a separate LIFO stack of freed `prog_number`s (`uint64`), pushed on COMPLEX delete and popped by `complex_node_to_record` to keep sidecar numbers dense.

The `<size>` in the filename is the byte size of the free region; it makes a pop an exact fit with no scan. `delete_node` populates the `nodes`/`complex`/`rel`/`edges` bins and `json_prog.dat`; `update_node_edges` (on every `add_edge` / inbound cleanup) populates the `rel`/`edges` bins (since 2026-06-07, [BUG-017](../legacy/known_bugs.md#2026-06-07--bug-017-update_node_edges-orfanizza-regioni-senza-spingerle-sulla-freelist)). On the read/reuse side, `insert` reuses the `nodes` bins (primitives) and the `complex` bins (COMPLEX), and since 2026-06-13 `update_node_edges` reuses the `rel`/`edges` bins (pop-then-append on edge rewrite). All four bin families are now reused. See the [freelist design decision](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo) and [Reuse of the rel/edges freelist bins](../legacy/design_decisions.md#2026-06-13--reuse-of-the-reledges-freelist-bins-edge-space-compaction).

### `attributes/attributes_meta.dat`

A single `JsonMeta` POD, 8 bytes:

```
offset  size  field
  0      8    prog_number   (uint64_t) — monotonic counter for unique sidecar names
```

Truncated and rewritten in full via `write_json_attributes_meta`. Lazy-created on first call to `read_json_attributes_meta` (with `prog_number = 0`).

### `attributes/{prog_number:020}_{type_label}.json`

The actual JSON attributes of a COMPLEX node, written as raw UTF-8 text (no length prefix, no framing). Filename composition:

- `prog_number` comes from `JsonMeta.prog_number` (or a recycled value from `json_prog.dat`) at write time, **zero-padded to `COMPLEX_PROG_DIGITS` (20)** — e.g. `00000000000000000000_Athlete.json`. The fixed width makes the record's on-disk size constant per type (see the [per-type binning decision](../legacy/design_decisions.md#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato)).
- `type_label` is the COMPLEX node's runtime label (e.g. `Athlete`, `Item`).

The exact filename **must** match the `json_file_path` string stored in the on-disk `ComplexHeader` so that the read path can reopen the file. Since 2026-05-30 (fix of [BUG-013](../legacy/known_bugs.md#2026-05-26--bug-013-path-del-file-json-sidecar-incoerente-tra-complex_node_to_record-e-write_complex)) the same `json_file_path` string is threaded through `complex_node_to_record` → `write_complex` → both `write_string` on `nodes.dat` and the sidecar `std::ofstream` open, so the two sides cannot diverge. On COMPLEX delete the sidecar is removed (`std::filesystem::remove`) and its `prog_number` recycled.

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

### Update ordering inside `update_node_edges` (push 2026-06-07, reuse 2026-06-13)

```
1. read nodes.idx[node_id]                     ← old NodeIndex
2. read nodes.dat at old relation_offset       ← old RelationNodeList + tail
3. push old RelationNodeList → rel bin, old edge chunks → edges bin; zero them; free_edge_count += chunks (BUG-017)
4. open nodes.dat (binary | in | out)          ← NOT app: seekp lands in-place on reuse, extends on append
5. rel_reuse = pop_free_offset(rel bin, sizeof(RelationNodeList)+batch_size)
     - hit  → seekp(rel_reuse.offset)           ← reuse the just-freed hole in place
     - miss → seekp(end); new_relation_offset = tellp()   ← append
   write the NEW RelationNodeList POD with current batch_size
6. for each relation in node.neighborgs:
     - open edges.dat (binary | in | out)
     - e_reuse = pop_free_offset(edges bin, edge_count*sizeof(Edge))
         - hit  → seekp(e_reuse.offset); free_edge_count--   ← reuse in place (ids NOT recycled)
         - miss → seekp(end); edge_offset = tellp()          ← append
     - write the relation's full edge chunk
     - append [name][new edge_offset][count] to nodes.dat   (contiguous put pointer, no seek)
7. open nodes.idx (binary | in | out)          ← NOT app: needs in-place seekp
8. seekp(node_id*sizeof(NodeIndex) + offsetof(NodeIndex, relation_offset))
9. write the NEW relation_offset (8 bytes, in place)
```
On a weight-overwrite the popped regions in steps 5/6 are exactly those freed in step 2 (LIFO, exact size) → in-place overwrite, no file growth.

## Dipendenze

**IN**: written/read exclusively by `graph_core/io/graph_io.{h,cpp}`.

**OUT**: filesystem only.

## Voci legacy collegate

- [Reuse of the rel/edges freelist bins (edge-space compaction)](../legacy/design_decisions.md#2026-06-13--reuse-of-the-reledges-freelist-bins-edge-space-compaction)
- [API — update_node_edges step 3: append-only → pop-then-append](../legacy/api_changes.md#2026-06-13--update_node_edges-step-3-append-only--pop-then-append)
- [Bin per-tipo per i record COMPLEX via prog_number zero-paddato](../legacy/design_decisions.md#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato)
- [Tombstone + azzeramento delle regioni su delete](../legacy/design_decisions.md#2026-06-07--tombstone--azzeramento-delle-regioni-su-delete)
- [Indice inverso degli archi entranti in-RAM](../legacy/design_decisions.md#2026-06-07--indice-inverso-degli-archi-entranti-in-ram)
- [BUG-016 — delete_node completata (fixed)](../legacy/known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex)
- [BUG-014 — prog_number incrementato/persistito (fixed)](../legacy/known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex)
- [Freelist a bin segregati per dimensione esatta + cancellazione nodo](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo)
- [API — FreeRecord rimossa, tre POD free-offset](../legacy/api_changes.md#2026-06-03--freerecord-rimossa-sostituita-da-tre-pod-free-offset)
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
- `graph_core/io/graph_io.cpp:247` — `update_node_edges` (step 2 pushes old regions onto `rel`/`edges` bins + zeroes; step 3 writes new RelationNodeList + edge chunks **pop-then-append** reusing exact-size holes, streams `in|out`; step 4 patches `NodeIndex.relation_offset` in place).
- `graph_core/odt/node_odt.cpp:27` — `node_to_relation_list` (now computes `batch_size`).
- `graph_core/io/io_utils.cpp:4` — `write_string` (length-prefixed).
- `graph_core/struct/pod_struct.h:182,196,209` — `NodeFreeOffset`, `RelationNodeListFreeOffset`, `BatchOfEdgesFreeOffset` (freelist bin records).
- `graph_core/io/graph_io.h:321,333,355` — `freelist_bin_path`, `write_free_offset`, `pop_free_offset` (bin path + push/pop).
- `graph_core/io/graph_io.cpp:380` — `delete_node_from_disk` (pushes the node's regions onto the bins).
