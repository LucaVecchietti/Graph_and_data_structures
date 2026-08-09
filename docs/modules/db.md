# Module: db (on-disk format)

> Binary file layout used by `graph_core/io/` to persist nodes, edges, and graph metadata.

| Campo | Valore |
|---|---|
| Tipo | module |
| Lingua | en |
| Ultimo aggiornamento | 2026-08-09 |
| Commit di riferimento | fbc6703 |
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
├── nodes.dat                             NodeRecord<T> | ComplexHeader+strings + NodeRelationList batch (appended on insert)
├── edges.dat                             Edge records, 48 B, doubly-linked per (node,relation) (appended on insert)
├── freelist/                             size-segregated free-offset bins (created on first delete)
│   ├── nodes_{size}.dat                  NodeFreeOffset records, one bin per primitive NodeRecord size
│   ├── complex_{size}.dat                NodeFreeOffset records, one bin per COMPLEX size class (per-type)
│   ├── rel_{size}.dat                    RelationNodeListFreeOffset records (size now constant 2213 → one bin)
│   ├── edges_{size}.dat                  BatchOfEdgesFreeOffset records, one bin per chunk size
│   └── json_prog.dat                     LIFO stack of freed prog_numbers (uint64) for COMPLEX sidecars
└── attributes/                           COMPLEX sidecars (created on first COMPLEX node)
    ├── attributes_meta.dat               JsonMeta (truncated + rewritten on every COMPLEX write)
    └── {prog_number:020}_{type_label}.json   JSON attributes payload per COMPLEX node (prog zero-padded)
```

There is no header magic, no version field, no checksum. Reading a `db/` produced by a build with a different `NodeType` enum or different POD layout will silently misinterpret data. The most recent schema break (2026-06-19) reshaped the relation list (`RelationNodeList` → `NodeRelationList`: 37-byte header + a fixed 2176-byte tail = 2213-byte batch) and grew `Edge` from 32 to 48 bytes (two chain offsets); before that, 2026-06-02 took `MetaRecord` from 24 to 48 bytes. Any `db/` produced before the relevant commit must be wiped.

The `attributes/` subtree only exists once at least one COMPLEX node has been written (or once `read_json_attributes_meta` is called and lazy-creates `attributes_meta.dat`). The full COMPLEX lifecycle (write, read, delete, slot reuse) runs end-to-end as of 2026-06-07: [BUG-014](../legacy/known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex) (`prog_number` now incremented + persisted, so sidecars no longer collide) and the COMPLEX part of [BUG-016](../legacy/known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex) are fixed. See the [sidecar JSON design decision](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex) and the [per-type binning decision](../legacy/design_decisions.md#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato).

## Design

- **Append-only data files** (`nodes.dat`, `edges.dat`) — opened with `std::ios::binary | std::ios::app`. Cheap inserts, no in-place updates.
- **`nodes.idx`: append-only on insert, in-place patch only on node relocation** — new entries appended by `write_node_index`; `update_node_edges` patches `NodeIndex.relation_offset` in place when it relocates a node's batch (now only on `delete_node`'s inbound cleanup). Since 2026-06-19 a normal `add_edge` does **not** touch `nodes.idx` — the batch stays put. See [Edge persistence](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch).
- **Truncated meta** — `meta.dat` (and `attributes/attributes_meta.dat`) are opened with `std::ios::binary | std::ios::trunc` and fully rewritten on every change. They're tiny (48 bytes / 8 bytes) so the cost is irrelevant.
- **Fixed-width `NodeIndex`** — allows O(1) lookup by id via `seekg(id * sizeof(NodeIndex))` in `nodes.idx`. Also enables the in-place patch above.
- **Type tag inside `NodeIndex`** — lets `read_node` dispatch to the right `read_typed_node<T>` without touching `nodes.dat`.
- **Edges stored separately, doubly-linked** — `edges.dat` holds all edges; each `NodeRelationList` tail line stores `(name, edge_offset, edge_count)` where `edge_offset` is the **head** of that relation's edge linked list (each `Edge` carries `prev_offset`/`next_offset`). Since 2026-06-19.
- **Fixed-width relation batches, chained** — a node's relations live in a `NodeRelationList` batch: a 37-byte header + a fixed `RELATION_BATCH_TAIL` (2176) byte tail of up to 8 fixed 272-byte lines, total 2213 bytes regardless of how few relations are used. A single line is addressable by index (`tail + i*272`), so one relation can be rewritten in place, and every batch is one freelist size class. Since 2026-06-19 — see the [decision](../legacy/design_decisions.md#2026-06-19--relation-list-a-batch-fixed-width--edge-a-lista-doppiamente-concatenata). Beyond 8 relation types the list continues in further batches linked by `next_offset` (`head` 1→2→3), each allocated on its own and therefore not necessarily contiguous; since 2026-08-09 — see the [chaining decision](../legacy/design_decisions.md#2026-08-09--relation-batch-chaining-catena-di-batch-via-next_offset).
- **Edge add/overwrite is O(1) (since 2026-06-19)** — a new edge is appended (or a freed 48 B slot reused), spliced at the relation's chain head (`prev_offset` of the old head patched in place), and the one fixed-width relation line is updated in place; the batch never moves so `NodeIndex` is untouched (`persist_new_edge`). A weight overwrite is an in-place 8-byte write at `edge_offset + offsetof(Edge, weight)` (`persist_edge_weight`). The whole-node rewrite (`update_node_edges`) survives only for `delete_node`'s inbound cleanup: it frees the old edges by chain-walk (per-edge 48 B slots → `edges` bin) and the old batch → `rel` bin, then re-lays the node contiguously, reusing an exact-size `rel` hole in place and refreshing each `EdgeRef.offset`. See the [O(1) add_edge decision](../legacy/design_decisions.md#2026-06-19--add_edge-in-o1-append--relink--in-place-line-update).
- **Freelist: size-segregated bins (since 2026-06-03)** — `db/freelist/<prefix>_<size>.dat`, one bin file per distinct free-region size. Push appends a free-offset record; pop reads the last record and truncates one record — both O(1), and a pop is always an exact fit. Since 2026-06-19 the sizes are standardized: one `rel_2213` bin (whole batch) and one `edges_48` bin (single `Edge`), so each is effectively a single bin. `delete_node` / `update_node_edges` push; `insert` pops `nodes`/`complex`, `persist_new_edge` pops `edges`. `FreeRecord` was removed in favour of three sized POD records (`NodeFreeOffset` / `RelationNodeListFreeOffset` / `BatchOfEdgesFreeOffset`, the last now one edge per record). `MetaRecord.free_edge_count` counts free **edges** (48 B slots): `free_edge_chain` bumps it per freed edge, `persist_new_edge` decrements it on a pop. See [Freelist a bin segregati](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo).
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
 16      8    relation_offset (uint64_t) → into nodes.dat (NodeRelationList first batch)
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
2. **`NodeRelationList` batch (header + fixed tail)** — total 2213 bytes (37-byte header, 2176-byte tail):
   ```
   offset  size  field
     0      8    node_id      (uint64_t) → owning node
     8      8    type_count   (uint64_t) → relation lines used in this batch (0..8)
    16      2    batch_size   (uint16_t) → reserved tail bytes, constant 2176
    18      2    free_bytes   (uint16_t) → batch_size - type_count*272
    20      8    next_offset  (uint64_t) → next batch of this node's chain, 0 if last
    28      8    head         (uint64_t) → batch serial: 1 first, 2,3,... extensions
    36      1    is_deleted   (uint8_t)
   ```
   Then a fixed `RELATION_BATCH_TAIL` (2176) byte tail of up to 8 **fixed 272-byte lines** (first `type_count` used, rest zero-filled):
   ```
   8    edge_offset   (uint64_t) → head of this relation's edge linked list in edges.dat
   8    edge_count    (uint64_t) → number of edges in that list
   1    name_length   (uint8_t)  → actual relation-name length (<= 255)
  255   name          (raw, zero-padded to 255 bytes)
   ```
   The total on-disk size of a batch is `sizeof(NodeRelationList) + batch_size = 37 + 2176 = 2213` bytes — constant. Line `i` is at `tail_start + i*272`.

   A node with more than 8 relation types owns a **chain** of batches (since 2026-08-09): `relation_offset` → the first, each `next_offset` → the following one (0 = last), `head` = 1, 2, 3… A node with `n` types owns `ceil(n / 8)` batches, minimum one. Lines `0..type_count-1` of a batch are used with **no holes**, and a new type always lands in the last batch (or in a fresh one appended to the chain). The batches are allocated independently (`rel` bin pop or append), so they are **not necessarily contiguous** — only `next_offset` defines the order. A walk is bounded by `RELATION_MAX_BATCHES` (4096) since the format carries no checksum. See the [decision](../legacy/design_decisions.md#2026-08-09--relation-batch-chaining-catena-di-batch-via-next_offset).

The byte offsets are recorded in the corresponding `NodeIndex` (`offset` → start of `NodeRecord<T>` or `ComplexHeader`, `relation_offset` → start of the **first** `NodeRelationList` header). Since 2026-06-19 a normal `add_edge` **does not move the batch**: `persist_new_edge` updates one relation line (and the header, for a new relation type) in place inside the existing 2213-byte region, so `NodeIndex.relation_offset` is untouched. That still holds when a new type overflows the last batch: the fresh batch is linked by patching `next_offset`, not by moving anything. Batches are only relocated (old chain → `rel` bin, new chain written pop-then-append, `relation_offset` patched to the first) by `update_node_edges` during `delete_node`'s inbound cleanup.

### `edges.dat`

A flat array of `Edge` POD records, 48 bytes each (packed; 32 → 48 on 2026-06-19, two chain offsets added):

```
offset  size  field
  0      8    id           (uint64_t) — globally-unique edge id from MetaRecord.next_edge_id
  8      8    weight       (int64_t)
 16      8    to_node      (uint64_t) — destination node id
 24      8    from_node    (uint64_t) — source node id
 32      8    prev_offset  (uint64_t) — previous edge of the same (node,relation) chain (0 = head)
 40      8    next_offset  (uint64_t) — next edge of the same chain (0 = tail)
```

Edges of a given `(node, relation)` form a **doubly-linked list**: the tail line's `edge_offset` points at the head, and each edge's `next_offset` walks the chain for `edge_count` steps (read path follows `next_offset`; `build_inbound_index` too). Since 2026-06-19 `add_edge` of a new edge **appends one `Edge`** (or reuses a freed 48 B slot) and **splices it at the chain head** in O(1): the new edge's `next` = old head, the old head's `prev_offset` is patched in place, and the relation line's `edge_offset` becomes the new head (`persist_new_edge`). So after O(1) inserts a chain is **not contiguous** — only `update_node_edges` (delete path) re-lays a chain contiguously. A weight overwrite is an in-place 8-byte write at `edge_offset + offsetof(Edge, weight)` → no growth. Edges are freed **one at a time** (a chain walk pushes each 48 B slot onto the `edges` bin); **edge ids are not recycled** (each `Edge` keeps its own id). Since 2026-06-02 each `Edge.id` is globally unique and stable: assigned once from `MetaRecord.next_edge_id` and preserved across rewrites. See [BUG-002 fixed](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi) and the [O(1) add_edge decision](../legacy/design_decisions.md#2026-06-19--add_edge-in-o1-append--relink--in-place-line-update).

### `freelist/<prefix>_<size>.dat`

Size-segregated free-offset bins, created lazily on the first `delete_node` (via `write_free_offset`, which `create_directories` on `db/freelist/`). One file per `(prefix, size)` pair; every record in a given file is the same fixed-size POD, so the file is a flat array. Bins are LIFO: push = append one record, pop = read the last record + `resize_file` down by one record.

| Prefix | Record POD | Region reclaimed |
|---|---|---|
| `nodes` | `NodeFreeOffset` (24 B: `idx`, `offset`, `size`) | a freed primitive `NodeRecord` region in `nodes.dat` + its reusable id slot in `nodes.idx`. `size ∈ {1,4,8}` for the current primitives. |
| `complex` | `NodeFreeOffset` (24 B: `idx`, `offset`, `size`) | a freed COMPLEX record region in `nodes.dat` + its reusable id slot. `size = (22 + COMPLEX_PROG_DIGITS) + 2·L` — constant per `type_label` length, so the bin is a per-type size class. |
| `rel` | `RelationNodeListFreeOffset` (16 B: `offset`, `size`) | a freed `NodeRelationList` batch (header + fixed tail) in `nodes.dat`. `size` is constant 2213 → a single `rel_2213.dat` bin. **One record per batch**: freeing a node whose relation list is a chain of `k` batches pushes `k` records (since 2026-08-09), and a chained batch allocation pops one. |
| `edges` | `BatchOfEdgesFreeOffset` (24 B: `idx`, `offset`, `size`) | a freed **single** `Edge` in `edges.dat`. `size` is constant `sizeof(Edge)` = 48 → a single `edges_48.dat` bin (since 2026-06-19; edges are no longer contiguous chunks). `idx` is the freed edge's id, ignored on reuse. |

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
   ┌──────────────────┐               ┌─────────────────────────┐         ┌───────────────┐
   │ NodeIndex { id=3 │               │ NodeRecord<int> { 42 }  │         │ Edge(head) ◀┐ │
   │   offset      ──┼───────────────▶│                         │         │   next ─▶   │ │
   │   relation_off ─┼───────┐        │ NodeRelationList(2213): │   ┌────▶│ Edge        │ │
   │   type_id=INT   │       │        │   header(37): node_id.. │   │     │   prev ─────┼─┘
   └──────────────────┘      │        │   type_count = 2        │   │     │ ...           │
                             └───────▶│   line "road": edge_off ┼───┘     └───────────────┘
                                      │   line "train": edge_off┼─────┐  edge_off → chain head
                                      │   line 2..7 (zeroed)    │     ▼  walk via next_offset
                                      └─────────────────────────┘  (train chain in edges.dat)
```

### Insert ordering inside `write_node`

```
1. open nodes.dat (binary | app)
2. seek end
3. dispatch on node_type_of_v<T>:
   - primitive T: write NodeRecord<T>             ← "offset" = tellp() before this write
   - COMPLEX:    write ComplexHeader + 2 strings ← "offset" = tellp() before this write
                 and write the sidecar JSON file under attributes/
4. write NodeRelationList batch: 37-byte header (type_count=0 for a fresh node)
        + full 2176-byte tail (all lines zeroed for a fresh node) → 2213 bytes
   ← "relation_offset" = tellp() before this write
   (per-relation: edge chain appended to edges.dat, then one fixed 272-byte line in nodes.dat)
5. open nodes.idx (binary | app)
6. write NodeIndex { id, offset, relation_offset, type_id }
```

### O(1) add ordering inside `persist_new_edge` (since 2026-06-19)

```
1. read nodes.idx[node_id].relation_offset       ← chain start (NOT modified below)
2. walk the batch chain (ifstream), hopping next_offset:
     stop on the line for `type`, else stop at the LAST batch
     (O(batches) = O(types/8); ≤8 types → one header read, as before)
3. allocate the Edge slot:
     - pop edges_48 bin → reuse a freed 48-byte slot (free_edge_count--)
     - else seekp(end) on edges.dat → new_off = tellp()
4. write Edge { id, weight, to, from, prev=0, next=old_head } at new_off
5. if relation existed and old_head != 0:
     seekp(old_head + offsetof(Edge, prev_offset)); write new_off   ← relink
6. update the relation line IN PLACE (nodes.dat in|out):
     - existing relation: seekp(found_batch + 37 + i*272); write [new_off][old_count+1]
     - new type, last batch has room: write a full line at slot type_count of the
       LAST batch, then rewrite its header with type_count+1 / free_bytes-272
     - new type, last batch FULL (since 2026-08-09): allocate a batch
         · pop rel_2213 bin → reuse a freed region, else seekp(end)
         · write header{head+1, type_count=1, next=0} + the line + zeroed tail
         · seekp(last_batch + offsetof(NodeRelationList, next_offset)); write it
   (nodes.idx is NEVER touched — no batch ever moves)
```
Weight overwrite (`persist_edge_weight`): `seekp(edge_offset + offsetof(Edge, weight))`, write 8 bytes. No allocation, no growth.

### Whole-node rewrite inside `update_node_edges` (delete path only, since 2026-06-19)

```
1. read nodes.idx[node_id] + the old batch CHAIN (collecting every batch offset)
2. free old edges per-edge: free_edge_chain walks each relation's chain,
   pushes each 48-byte slot → edges_48 bin, zeroes it, free_edge_count++
   push EVERY old batch → rel_2213 bin; zero each
3. n_batches = ceil(types / 8), min 1. FIRST decide where each one lands:
     - pop rel_2213 bin per batch → reuse a just-freed hole in place
     - miss → successive EOF slots (EOF + k*2213; probing EOF twice without
              writing would hand back the same offset)
   (deciding up front is required: each next_offset must point at the next batch)
4. for each batch b: seekp(offset[b]); write header{head=b+1, next=offset[b+1] or 0};
   for each of its ≤8 relations: append a fresh CONTIGUOUS edge run at EOF of
   edges.dat, refresh each EdgeRef.offset, write its fixed 272-byte line;
   then pad the tail. dat_out is seeked ONLY at batch boundaries.
5. patch nodes.idx[node_id].relation_offset in place → offset[0]
```

## Dipendenze

**IN**: written/read exclusively by `graph_core/io/graph_io.{h,cpp}`.

**OUT**: filesystem only.

## Voci legacy collegate

- [Relation-batch chaining: catena di batch via next_offset](../legacy/design_decisions.md#2026-08-09--relation-batch-chaining-catena-di-batch-via-next_offset)
- [API — Firme relation-batch: relation_batch_header, read_relation_node_list con batch_offsets](../legacy/api_changes.md#2026-08-09--firme-relation-batch-relation_batch_header-read_relation_node_list-con-batch_offsets)
- [add_edge in O(1): append + relink + in-place line update](../legacy/design_decisions.md#2026-06-19--add_edge-in-o1-append--relink--in-place-line-update)
- [API — add_edge O(1): EdgeRef.offset + persist_new_edge / persist_edge_weight](../legacy/api_changes.md#2026-06-19--add_edge-o1-edgerefoffset--persist_new_edge--persist_edge_weight)
- [Relation-list a batch fixed-width + Edge a lista doppiamente concatenata](../legacy/design_decisions.md#2026-06-19--relation-list-a-batch-fixed-width--edge-a-lista-doppiamente-concatenata)
- [API — RelationNodeList → NodeRelationList (tail fixed-width)](../legacy/api_changes.md#2026-06-19--relationnodelist--noderelationlist-header-esteso--tail-fixed-width)
- [API — Edge: prev_offset / next_offset](../legacy/api_changes.md#2026-06-19--edge-aggiunti-prev_offset--next_offset-32--48-byte)
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
- `graph_core/struct/pod_struct.h` — POD layouts for `NodeIndex`, `NodeRecord`, `NodeRelationList`, `Edge`, `MetaRecord`, `ComplexHeader`, `JsonMeta`, and the freelist `NodeFreeOffset` / `RelationNodeListFreeOffset` / `BatchOfEdgesFreeOffset` (`FreeRecord` removed 2026-06-03).
- `graph_core/struct/pod_struct.h:136` — `MetaRecord` (now 48 bytes: 3 node + 3 edge counters).
- `graph_core/struct/domain_struct.h:24` — `EdgeRef { id, weight, neighbor }` (RAM-side edge, source of `Edge.id`).
- `graph_core/graph.cpp:57` — `add_edge` (assigns edge id, bumps `next_edge_id`/`edge_count`).
- `graph_core/struct/pod_struct.h` — `NodeRelationList` (37-byte header + fixed 2176-byte tail = 2213-byte batch; renamed from `RelationNodeList` 2026-06-19) and `Edge` (48 bytes: + `prev_offset`/`next_offset`).
- `graph_core/costants.h` — `RELATION_LINE_SIZE` (272), `RELATION_LINES_PER_BATCH` (8), `RELATION_BATCH_TAIL` (2176), `RELATION_NAME_MAX` (255).
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
- `graph_core/io/graph_io.cpp` — `persist_new_edge` (O(1) add: append/reuse + relink + in-place line) and `persist_edge_weight` (O(1) in-place weight); `free_edge_chain` (per-edge reclaim, anon namespace).
- `graph_core/io/graph_io.cpp` — `update_node_edges` (delete-path only since 2026-06-19: step 2 frees old edges via `free_edge_chain` + pushes old batch onto `rel` bin; step 3 reuses an exact-size `rel` hole + appends fresh contiguous edge runs + refreshes `EdgeRef.offset`; step 4 patches `relation_offset`).
- `graph_core/io/graph_io.h` — `write_relation_line` / `read_relation_line` / `pad_relation_tail` / `write_edge_chain_at` (fixed-width batch + edge-chain helpers).
- `graph_core/struct/domain_struct.h` — `EdgeRef { id, weight, neighbor, offset }` (`offset` = disk position, for O(1) overwrite).
- `graph_core/odt/node_odt.cpp:32` — `relation_batch_header(node_id, lines_in_batch, head, next_offset)` (builds one fixed-width batch header of the chain).
- `graph_core/io/io_utils.cpp:4` — `write_string` (length-prefixed).
- `graph_core/struct/pod_struct.h:182,196,209` — `NodeFreeOffset`, `RelationNodeListFreeOffset`, `BatchOfEdgesFreeOffset` (freelist bin records).
- `graph_core/io/graph_io.h:321,333,355` — `freelist_bin_path`, `write_free_offset`, `pop_free_offset` (bin path + push/pop).
- `graph_core/io/graph_io.cpp:380` — `delete_node_from_disk` (pushes the node's regions onto the bins).
