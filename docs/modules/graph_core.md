# Module: graph_core

> The C++17 core of pointer_graphs. Defines the `Graph` class, the dual POD/domain struct hierarchy, the ODT translation layer, and the binary I/O layer.

| Campo | Valore |
|---|---|
| Tipo | module |
| Lingua | en |
| Ultimo aggiornamento | 2026-08-09 |
| Commit di riferimento | fbc6703 |
| Mirror | — |

---

## Overview

`graph_core` is the in-process graph engine. It keeps nodes in a `std::unordered_map<int, BaseNode*>` for O(1) random access by id, persists every insert immediately to the `db/` files, and exposes generic BFS/DFS via a policy-based traversal template.

## Struttura

```
graph_core/
├── graph.h                     Graph class (header-only templates + declarations)
├── graph.cpp                   Graph non-template implementation
├── costants.h                  Compile-time constants (RELATION_TYPE_MAX_SIZE, fixed-width batch sizes, DB_PATH)
├── logger.h                    Header-only file logger
├── struct/
│   ├── domain_struct.h         BaseNode (type-erased) + Node<T> (typed payload)
│   ├── pod_struct.h            On-disk POD layouts (packed)
│   ├── functions_policies.h    BFSPolicy / DFSPolicy
│   ├── type_registry.h         Compile-time C++ type ↔ NodeType mapping (+ node_record_payload_size decl)
│   └── type_registry.cpp       node_record_payload_size (out-of-line, non-template)
├── odt/
│   ├── node_odt.h/cpp          Domain Node<T> ↔ POD NodeRecord/NodeRelationList/NodeIndex
│   └── edge_odt.h/cpp          Edge POD builder
└── io/
    ├── graph_io.h/cpp          High-level read/write of nodes, edges, meta
    └── io_utils.h/cpp          Low-level POD/string/offset helpers
```

## Design

The module is layered to isolate three concerns:

1. **Domain shape** (RAM, polymorphic, `std::unordered_map`-based) — defined in `struct/domain_struct.h`.
2. **Persistence shape** (packed POD, fixed-width fields) — defined in `struct/pod_struct.h`.
3. **Translation** — `odt/` converts between (1) and (2).

The split lets each layer be optimized for its purpose: RAM uses hash maps and pointers; disk uses sequential POD records.

See:
- [POD vs Domain split](../legacy/design_decisions.md#2026-05-26--separazione-pod-vs-domain-struct)
- [Type-erased BaseNode + Node&lt;T&gt;](../legacy/design_decisions.md#2026-05-26--type-erased-basenode--nodet)
- [Policy-based traversal](../legacy/design_decisions.md#2026-05-26--policy-based-traversal-bfsdfs)
- [Single-open append for nodes.dat](../legacy/design_decisions.md#2026-05-26--single-open-append-su-nodesdat)

## Tipi e strutture dati

### `enum class LogLevel` (`logger.h`)
Severity level for `Logger`. Values: `DEBUG`, `INFO`, `WARN`, `ERR`.

### `class Logger` (`logger.h`)
File + stderr logger. Constructor opens the log file in append mode and throws `std::runtime_error` if it cannot. `debug/info/warn/error` route through `log(LogLevel, std::string)` which filters by `min_level`, prepends timestamp + level tag, writes to file and to `std::cerr`, then flushes.

### Constants (`costants.h`)
| Name | Type | Value | Purpose |
|---|---|---|---|
| `RELATION_TYPE_MAX_SIZE` | `constexpr uint8_t` | `255` | Max length of a relation-type string in `add_edge`. |
| `RELATION_NAME_MAX` | `constexpr uint8_t` | `255` | Fixed name-field width per relation line (name zero-padded to this). |
| `RELATION_LINE_SIZE` | `constexpr uint16_t` | `272` | Size of one fixed-width relation line: `8 (edge_offset) + 8 (edge_count) + 1 (name_length) + 255 (name)`. |
| `RELATION_LINES_PER_BATCH` | `constexpr uint8_t` | `8` | Max relation lines per batch. A 9th relation type spills into a chained batch (since 2026-08-09), it no longer throws. |
| `RELATION_BATCH_TAIL` | `constexpr uint16_t` | `2176` | Reserved tail bytes per batch (`RELATION_LINE_SIZE * RELATION_LINES_PER_BATCH`). Always written full-width (unused lines zeroed) → every batch is one size class. |
| `RELATION_MAX_BATCHES` | `constexpr uint32_t` | `4096` | Bound on a relation-chain walk. The format has no checksum, so every walker throws past this many batches rather than looping on a corrupted `next_offset`. Added 2026-08-09. |
| `DB_PATH` | `constexpr std::string_view` | `"../db"` | Root directory of the on-disk store (relative to the build run dir). |
| `META_FILE_PATH` | `constexpr std::string_view` | `"../db/meta.dat"` | Path to the meta file. Declared but currently unused — `write_meta`/`read_meta` still compose the path inline from `DB_PATH`. |
| `JSON_ATTR_META_PATH` | `constexpr std::string_view` | `"../db/attributes/attributes_meta.dat"` | Path to the `JsonMeta` POD used to track unique JSON sidecar names. |
| `JSON_ATTR_PATH` | `constexpr std::string_view` | `"../db/attributes/"` | Base directory for JSON sidecar files attached to COMPLEX nodes. |
| `COMPLEX_PROG_DIGITS` | `constexpr uint8_t` | `20` | Fixed width of the zero-padded `prog_number` prefix in a COMPLEX sidecar filename. 20 covers the full `uint64` range, making a COMPLEX record's on-disk size a pure function of `type_label` length so the exact-size freelist bins act as per-type size classes. See the [binning decision](../legacy/design_decisions.md#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato). |

### `struct EdgeRef` (`struct/domain_struct.h`)
RAM-side description of one outgoing edge. Inner value type of the adjacency map. Introduced 2026-06-02 to carry the edge id alongside the weight (see [API change](../legacy/api_changes.md#2026-06-02--basenodeneighborgs-da-pairint-basenode-a-edgeref)).
| Field | Type | Purpose |
|---|---|---|
| `id` | `uint64_t` | Globally-unique edge id. Source of truth is `MetaRecord.next_edge_id`; assigned once when the edge is first added, then preserved across the full-node rewrites of `update_node_edges`. On disk it is `Edge.id`. |
| `weight` | `int` | Edge weight. |
| `neighbor` | `BaseNode*` | Pointer to the destination node. `nullptr` until re-linked after a load from disk (`read_node` leaves it unset). |
| `offset` | `uint64_t` | Byte offset of this edge's `Edge` POD in `edges.dat` (added 2026-06-19). Lets `add_edge` overwrite the weight in O(1) (`persist_edge_weight`) without walking the chain. Set on load (`read_typed_node`), on append (`persist_new_edge`), and refreshed by `update_node_edges` when it relocates the node's edges. |

### `struct BaseNode` (`struct/domain_struct.h`)
Type-erased base. Holds only the adjacency map:
```cpp
std::unordered_map<std::string,
    std::unordered_map<int, EdgeRef>> neighborgs;
```
Outer key: relation type. Inner key: neighbor id. Inner value: `EdgeRef(id, weight, neighbor-ptr)`.

Virtual destructor so deleting through `BaseNode*` frees the derived `Node<T>` correctly.

### `template<class T> struct Node : BaseNode` (`struct/domain_struct.h`)
Adds the typed payload `T data;`. `T` is constrained at use site to be POD (trivially copyable) by `node_to_record`'s `static_assert`.

### `enum class NodeType : uint8_t` (`struct/pod_struct.h`)
On-disk tag for `T`. Values: `INT=0, FLOAT=1, DOUBLE=2, CHAR=3, BOOL=4, TOMBSTONE=254, COMPLEX=255`. The integer values are stable — changing them breaks the on-disk format. `TOMBSTONE` (added 2026-06-07) marks a logically-deleted `nodes.idx` slot: the entry survives (its id stays reusable via the freelist) but its offsets are zeroed and the on-disk record/relation/edge bytes have been zero-filled; `read_node` on a tombstoned id throws, and a later reuse overwrites the whole `NodeIndex`, clearing it. See the [tombstone decision](../legacy/design_decisions.md#2026-06-07--tombstone--azzeramento-delle-regioni-su-delete). `COMPLEX` is for records carrying a runtime `type_label` + JSON attributes (see `ComplexHeader` and `ComplexRecord` below); its full insert→write→read→delete→reuse path is implemented (sidecar JSON design [here](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex)). Future primitive types should use values `5..253`.

### `struct NodeIndex` (POD, packed) (`struct/pod_struct.h`)
Fixed-width entry stored in `nodes.idx`.
| Field | Type | Purpose |
|---|---|---|
| `id` | `uint64_t` | Node id (matches the in-memory key). |
| `offset` | `uint64_t` | Byte offset into `nodes.dat` where the `NodeRecord<T>` starts. |
| `relation_offset` | `uint64_t` | Byte offset into `nodes.dat` where the `NodeRelationList` (first batch) for this node starts. |
| `type_id` | `NodeType` | Dispatch tag used by `read_node` to pick the right `read_typed_node<T>`. |

### `template<class T> struct NodeRecord` (POD, packed) (`struct/pod_struct.h`)
Just `T data;`. `T` must be trivially copyable (asserted at use site).

### `struct NodeRelationList` (POD, packed) (`struct/pod_struct.h`)
Header of one **fixed-width relation batch**. 37 bytes; renamed from `RelationNodeList` and reshaped 2026-06-19 (see the [decision](../legacy/design_decisions.md#2026-06-19--relation-list-a-batch-fixed-width--edge-a-lista-doppiamente-concatenata) and [API change](../legacy/api_changes.md#2026-06-19--relationnodelist--noderelationlist-header-esteso--tail-fixed-width)). The header is followed by a **fixed `RELATION_BATCH_TAIL` (2176) byte** tail, so the whole batch region is always **2213 bytes**.
| Field | Type | Purpose |
|---|---|---|
| `node_id` | `uint64_t` | Owning node id (back-reference). |
| `type_count` | `uint64_t` | Number of relation lines actually used in this batch (0..8). Lines `0..type_count-1` are used with **no holes**: a new relation type always goes into the last batch of the chain, or into a fresh one. |
| `batch_size` | `uint16_t` | Reserved tail size in bytes — constant `RELATION_BATCH_TAIL` (2176). Reclaimable region = `sizeof(NodeRelationList) + batch_size = 2213`. |
| `free_bytes` | `uint16_t` | Free tail bytes = `batch_size - type_count * RELATION_LINE_SIZE` (multiple of 272). |
| `next_offset` | `uint64_t` | Offset of the next batch in `nodes.dat`, or 0 if last. **Live since 2026-08-09** ([decision](../legacy/design_decisions.md#2026-08-09--relation-batch-chaining-catena-di-batch-via-next_offset)): batches of one chain are allocated independently, so they are not necessarily contiguous — only this field defines the order. |
| `head` | `uint64_t` | Batch serial number: 1 = first batch, 2,3,… = extensions. |
| `is_deleted` | `uint8_t` | 1 if the batch is a freed region, 0 if live. |

The tail holds up to `RELATION_LINES_PER_BATCH` (8) **fixed-width** lines `[uint64_t edge_offset][uint64_t edge_count][uint8_t name_length][char name[255]]`, the first `type_count` used and the rest zero-filled. Line `i` lives at `tail + i * RELATION_LINE_SIZE`, so a single relation's `edge_offset`/`edge_count` can be rewritten in place. Lines are written by `write_relation_node_list` (insert) / `update_node_edges` (edge update) via the shared `write_relation_line` / `pad_relation_tail` helpers; `edge_offset` points at the **head** of that relation's edge linked list.

A node's relation list is a **chain** of these batches (since 2026-08-09): `NodeIndex.relation_offset` → first batch, `next_offset` → the following one (0 = last), `head` numbering them 1, 2, 3… A node with `n` relation types owns `ceil(n / 8)` batches (minimum one, so `relation_offset` always points at a readable header). The layout helpers live in `io/graph_io.h`: `relation_batch_region_size()` (constant 2213), `relation_batch_count(n)` (`ceil(n/8)`, min 1) and `relation_lines_in_batch(n, b)`.

### `struct Edge` (POD, packed) (`struct/pod_struct.h`)
48 bytes since 2026-06-19 (was 32 — added the two chain pointers; see [API change](../legacy/api_changes.md#2026-06-19--edge-aggiunti-prev_offset--next_offset-32--48-byte)). Edges of one `(node, relation)` pair form a **doubly-linked list**.
| Field | Type | Purpose |
|---|---|---|
| `id` | `uint64_t` | Globally-unique edge id, sourced from `MetaRecord.next_edge_id` (since 2026-06-02 — [BUG-002](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi) fixed). Written from `EdgeRef.id`. |
| `weight` | `int64_t` | Edge weight. |
| `to_node` | `uint64_t` | Destination node id. |
| `from_node` | `uint64_t` | Source node id. |
| `prev_offset` | `uint64_t` | Offset of the previous edge of the same `(node, relation)` chain, or 0 if head. |
| `next_offset` | `uint64_t` | Offset of the next edge of the same chain, or 0 if tail. |

### `struct MetaRecord` (POD, packed) (`struct/pod_struct.h`)
| Field | Type | Purpose |
|---|---|---|
| `next_id` | `uint64_t` | Next node id to assign on insert. |
| `node_count` | `uint64_t` | Total nodes ever inserted. |
| `free_count` | `uint64_t` | Count of free node slots across all freelist bins. `delete_node_from_disk` increments it (+1), the reuse path of `Graph::insert` decrements it (−1). Maintained since 2026-06-07. |
| `edge_count` | `uint64_t` | Number of live edges. Incremented by `add_edge` for each genuinely new edge (since 2026-06-02). |
| `next_edge_id` | `uint64_t` | Next edge id to assign. Monotonic source for `Edge.id` / `EdgeRef.id` (since 2026-06-02 — [BUG-002](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi) fixed). |
| `free_edge_count` | `uint64_t` | Count of freed edge chunks (one freelist record per orphaned `(node, relation)` chunk). Bumped by `delete_node_from_disk` (the deleted node's chunks) and by `update_node_edges` on push (the node's old chunks on every `add_edge` / inbound cleanup); since 2026-06-13 `update_node_edges` also **decrements** it on each `edges`-bin reuse, so on a weight-overwrite the count round-trips. Maintained since 2026-06-07 ([BUG-017](../legacy/known_bugs.md#2026-06-07--bug-017-update_node_edges-orfanizza-regioni-senza-spingerle-sulla-freelist)). |

POD layout grew from 24 to 48 bytes on 2026-06-02 (the three edge fields) — see [API change](../legacy/api_changes.md#2026-06-02--metarecord-aggiunti-i-campi-edge_count-next_edge_id-free_edge_count). Any `db/meta.dat` produced before that must be wiped.

### Freelist free-offset PODs (POD, packed) (`struct/pod_struct.h`)
Three records describing a reclaimable on-disk region, pushed onto the size-segregated freelist bins under `db/freelist/`. They replace the old single-field `FreeRecord` (removed 2026-06-03 — see [API change](../legacy/api_changes.md#2026-06-03--freerecord-rimossa-sostituita-da-tre-pod-free-offset)). All packed. Written/read via the `write_free_offset` / `pop_free_offset` templates. See the [freelist design decision](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo).

**`struct NodeFreeOffset`** — a freed `NodeRecord` region in `nodes.dat` + the now-reusable id slot in `nodes.idx`.
| Field | Type | Purpose |
|---|---|---|
| `idx` | `uint64_t` | Reusable node id (its fixed-width slot in `nodes.idx`). |
| `offset` | `uint64_t` | Start of the orphaned `NodeRecord` in `nodes.dat`. |
| `size` | `uint64_t` | Byte size of the free region (selects the bin). |

**`struct RelationNodeListFreeOffset`** — a freed `NodeRelationList` batch region (header + fixed tail) in `nodes.dat`. No id: a relation batch has no standalone id, only its byte region. (The POD keeps its name; only the relation-list struct it describes was renamed.)
| Field | Type | Purpose |
|---|---|---|
| `offset` | `uint64_t` | Start of the orphaned `NodeRelationList` batch in `nodes.dat`. |
| `size` | `uint64_t` | `sizeof(NodeRelationList) + batch_size` — constant 2213 since 2026-06-19, so one size class. |

**`struct BatchOfEdgesFreeOffset`** — a freed **single** `Edge` slot in `edges.dat`. Since 2026-06-19 edges are a linked list (no longer contiguous), so they are freed/reused one at a time; `size` is always `sizeof(Edge)` (48) → one `edges_48` bin. (The struct keeps its name and three fields.)
| Field | Type | Purpose |
|---|---|---|
| `idx` | `uint64_t` | The freed edge's id (ignored on reuse — each new `Edge` keeps its own id). |
| `offset` | `uint64_t` | Start of the orphaned 48-byte `Edge` in `edges.dat`. |
| `size` | `uint64_t` | `sizeof(Edge)` (48). |

### `struct ComplexHeader` (POD, packed) (`struct/pod_struct.h`)
Header for `COMPLEX` nodes on disk.
| Field | Type | Purpose |
|---|---|---|
| `type_label_size` | `uint64_t` | Byte length of the type-label string that follows. |
| `json_file_path_size` | `uint64_t` | Byte length of the JSON-file-path string that follows. |

The two strings (`type_label` then `json_file_path`) are written **after** the header as raw bytes (no NUL terminator), in this order. The actual JSON attributes are not inlined: they live in a sidecar file under `db/attributes/` whose path is recorded in `json_file_path`. See the [sidecar JSON design decision](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex). The field was previously named `json_attributes_size` — see the [API change](../legacy/api_changes.md#2026-05-26--complexheaderjson_attributes_size--json_file_path_size).

### `struct JsonMeta` (POD, packed) (`struct/pod_struct.h`)
Metadata for the COMPLEX sidecar file naming scheme.
| Field | Type | Purpose |
|---|---|---|
| `prog_number` | `uint64_t` | Monotonic counter used to compose unique sidecar names. Persisted in `db/attributes/attributes_meta.dat`. The name is `{prog_number:020}_{type_label}.json` — the number is **zero-padded to `COMPLEX_PROG_DIGITS` (20)** since 2026-06-07, so the record size is constant per type. |

`read_json_attributes_meta` lazy-creates the file with `prog_number = 0` on first access. Since 2026-06-07 the counter **is** advanced and persisted by `complex_node_to_record` ([BUG-014](../legacy/known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex) fixed): a fresh number is consumed (and `prog_number+1` persisted) only when no recycled number is available from the json free list (`db/freelist/json_prog.dat`, freed on COMPLEX delete).

### `struct ComplexRecord` (`struct/domain_struct.h`)
RAM-side representation of a `COMPLEX` node payload. Not POD — contains `std::string`.
| Field | Type | Purpose |
|---|---|---|
| `type_label` | `std::string` | Runtime-typed label (e.g. `"Athlete"`, `"Item"`, `"Company"`). |
| `json_attributes` | `std::string` | JSON-encoded attributes of the record (lives in RAM; on disk it is written to a sidecar file, not inline). |

Because `ComplexRecord` is not trivially copyable, it **cannot** flow through the existing `node_to_record` / `write_pod` path: that template would fail the `static_assert` in `odt/node_odt.h:22`. The COMPLEX branch of `write_node` routes through `complex_node_to_record` (computes the sidecar path) + `write_complex` (writes the header, the two length-prefixed strings, and the sidecar JSON file). The full COMPLEX lifecycle is implemented: insert (append or reuse via `write_complex_in_freed_slot`), read (`read_complex`), and delete (`delete_node_from_disk` reads the header for the real size, removes the sidecar, recycles the `prog_number`). The previously-open COMPLEX items [BUG-014](../legacy/known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex) and the COMPLEX part of [BUG-016](../legacy/known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex) are fixed (2026-06-07).

### `template<class T> struct node_type_of` (`struct/type_registry.h`)
Compile-time `T → NodeType` map. Primary template is intentionally undefined; specializations exist for `int, float, double, char, bool` and `ComplexRecord` (→ `COMPLEX`). Using an unsupported `T` triggers a clear compile error. Convenience: `node_type_of_v<T>`. The `ComplexRecord → COMPLEX` mapping is defined and `write_node` / `read_typed_node` / `Graph::insert` all dispatch on it via `if constexpr`. The full insert→write→read path compiles since 2026-05-30 ([BUG-010](../legacy/known_bugs.md) and [BUG-015](../legacy/known_bugs.md) fixed).

### `size_t node_record_payload_size(NodeType)` (`struct/type_registry.h` decl, `type_registry.cpp` def)
Non-template runtime helper: on-disk payload size of a `NodeRecord` for a given `NodeType` (`INT`→4, `FLOAT`→4, `DOUBLE`→8, `CHAR`→1, `BOOL`→1, `COMPLEX`→`sizeof(ComplexHeader)`; `throw std::runtime_error` on unknown tag, including `TOMBSTONE`). Defined **out-of-line** in `type_registry.cpp` (added to `CMakeLists.txt` 2026-06-03) so it has a single definition across the program — a header body would be a multiple-definition/ODR error. Used to pick the freelist bin for primitives in `delete_node_from_disk` (push) and `Graph::insert`'s reuse path (pop). **COMPLEX is no longer routed through it:** since 2026-06-07 the COMPLEX paths use the real variable-width size — `complex_record_on_disk_size(L)` on insert and the on-disk `ComplexHeader` on delete (see [binning decision](../legacy/design_decisions.md#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato)).

### `struct BFSPolicy` / `struct DFSPolicy` (`struct/functions_policies.h`)
Concept-style policies. Each provides:
- `Frontier` type (`std::queue<int>` for BFS, `std::stack<int>` for DFS),
- `static void push(Frontier&, int)`,
- `static int pop(Frontier&)`,
- `static bool empty(Frontier&)`.

The traversal algorithm in `Graph::traverse` is identical; only the policy changes.

### `struct RelationEntry` (`io/graph_io.h`)
In-memory form of one relation-type entry parsed from a `NodeRelationList` tail line. Fields: `std::string name; uint64_t edge_offset; uint64_t edge_count;`.

### `class Graph` (`graph.h`)
**Members:**
- `std::unordered_map<int, BaseNode*> nodes` — owns all heap-allocated nodes; freed in the destructor.
- `std::unordered_map<int, std::unordered_set<int>> in_edges` — inbound (reverse) edge index `to_id → {from_id}`. In-RAM only, never persisted; rebuilt from disk at load by `build_in_edges` and maintained incrementally by `add_edge` / `delete_node`. Lets `delete_node` find inbound edges in O(deg_in). Added 2026-06-07 — see the [reverse-index decision](../legacy/design_decisions.md#2026-06-07--indice-inverso-degli-archi-entranti-in-ram).
- `MetaRecord meta` — in-RAM copy of `meta.dat`.
- `Logger logger` — writes to `graph.log` with `DEBUG` floor.

**Private methods:**
- `void init_meta()` — zero-fill `meta` and write `meta.dat`. Called by the constructor when no `meta.dat` exists.
- `void load_meta()` — read `meta.dat` into `meta`.
- `void build_in_edges()` — `in_edges = build_inbound_index(meta.next_id)`. Called by the constructor on the load path (existing DB). One O(N+E) disk scan.

**Public methods:** see next section.

## Funzioni / interfacce esposte

### `Graph::Graph()` (`graph.cpp:8`)
Creates `DB_PATH` if missing. If `meta.dat` doesn't exist or is zero-sized, calls `init_meta()`; otherwise `load_meta()` followed by `build_in_edges()` (one O(N+E) scan to rebuild the reverse index — the only work the constructor does beyond reading `meta` on the load path).

### `Graph::~Graph()` (`graph.cpp:21`)
`delete`s every node in `nodes`. The map itself is destroyed by the `unordered_map` destructor.

### `template<class T> void Graph::insert(T&& value)` (`graph.h:43`)
Allocates a `Node<ValueType>` (with `T` stripped of reference) and forwards `value` into `data`. Then, since 2026-06-03, picks one of two paths:

- **Reuse path**: tries to `pop_free_offset<NodeFreeOffset>` from the right size bin. An `if constexpr` dispatches by type:
  - **primitives** (`!= COMPLEX`): bin `freelist_bin_path("nodes", node_record_payload_size(...))`; on a hit writes the record in place via `write_node_in_freed_slot`.
  - **COMPLEX** (since 2026-06-07): bin `freelist_bin_path("complex", complex_record_on_disk_size(type_label.size()))`; on a hit writes via `write_complex_in_freed_slot`. The fixed per-type size makes the hit an exact fit.
  On a hit, recycles the slot's `idx` as the node id, does `meta.node_count++` and `meta.free_count--`, but **`meta.next_id` is NOT bumped** (the id was reused). The INFO log marks `(reused slot)`.
- **Append path** (no fitting hole): mints `node_id = meta.next_id`, calls `write_node(*newNode, meta)`, then `meta.node_count++` and `meta.next_id++`.

Both paths place the node in `nodes[node_id]`, log at INFO, then `write_meta(meta)`. The `if constexpr` split keeps each writer instantiated only for its types (`write_node_in_freed_slot<ComplexRecord>` would fail `node_to_record`'s `static_assert`; `write_complex_in_freed_slot` needs the `ComplexRecord` payload).

- Throws: whatever the write helpers / `pop_free_offset` / `write_meta` throw (runtime_error on file open/truncate failure).
- Side effects: writes to `db/nodes.dat`, `db/nodes.idx`, `db/edges.dat` (empty edges section), `db/meta.dat`; on the reuse path also truncates a `db/freelist/{nodes,complex}_<size>.dat` bin; for COMPLEX also writes the sidecar under `db/attributes/`; appends to `graph.log` and stderr.

### `void Graph::delete_node(int node_id)` (`graph.cpp:163`)
Fully deletes a node (added 2026-06-03, completed 2026-06-07 — [BUG-016](../legacy/known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex) closed). See [API change](../legacy/api_changes.md#2026-06-03--nuova-graphdelete_nodeint).
1. If `node_id` not in RAM: lazy-load via `read_node` when `node_id < meta.next_id` (same pattern as `add_edge`), else throw `std::out_of_range`. A failed reload throws `std::runtime_error`.
2. Logs the count of outgoing edges. **Reverse-index outbound cleanup:** for every neighbor the node points at, removes `node_id` from `in_edges[neighbor]`. Decrements `meta.edge_count` by the node's outgoing edge count.
3. **Inbound cleanup:** for each owner in `in_edges[node_id]`, loads it (lazy), erases `node_id` from every relation of its adjacency (dropping relations left empty), re-persists it via `update_node_edges`, and decrements `meta.edge_count` per removed edge. Then drops `in_edges[node_id]`. This is what prevents dangling neighbors after a reload.
4. Erases the node from `nodes` and `delete`s the pointer.
5. Calls `delete_node_from_disk(node_id, meta)` (orphans + zeroes the node's regions, tombstones its idx slot, removes the COMPLEX sidecar if any, updates `node_count`/`free_count`/`free_edge_count`), then `write_meta(meta)`.

### `void Graph::add_edge(int start, int end, std::string type = "", int weight = 1)` (`graph.cpp:53`)
1. Rejects `type` longer than `RELATION_TYPE_MAX_SIZE` (throws `std::invalid_argument`).
2. For each endpoint not in RAM: if id `< meta.next_id`, `read_node` from disk and cache; else throw `std::out_of_range`.
3. Resolves the edge id and whether it is new: a brand-new `(start, type, end)` triple consumes a fresh id from `meta.next_edge_id`; an existing triple reuses the id **and disk offset** already stored in its `EdgeRef` (only the weight is overwritten).
4. **O(1) persist (since 2026-06-19):**
   - **New edge:** `persist_new_edge(meta, start, type, end, edge_id, weight)` appends/reuses one `Edge`, splices it at the relation's chain head, and updates that one fixed-width relation line in place (the batch never moves → `nodes.idx` untouched). The returned disk offset is stored into `EdgeRef{id, weight, end_ptr, offset}`; then `in_edges[end].insert(start)`, `meta.next_edge_id++`, `meta.edge_count++`.
   - **Overwrite:** updates the in-RAM `EdgeRef.weight`/`.neighbor` and calls `persist_edge_weight(existing_offset, weight)` — an in-place 8-byte write.
5. `write_meta(meta)` unconditionally (a new edge moves counters; a reuse pop inside `persist_new_edge` may move `free_edge_count`).

**Side effects on disk (O(1) path, since 2026-06-19):** a new edge writes one `Edge` to `edges.dat` (append or a reused 48 B slot), patches the old chain head's `prev_offset` in place, and rewrites one relation line (+ the header for a brand-new relation type) in place — `nodes.dat` does not grow, `edges.dat` grows by ≤48 B, `nodes.idx` is **not** touched. An overwrite writes 8 bytes in place (no growth). `add_edge` no longer calls `update_node_edges`. See the [O(1) add_edge decision](../legacy/design_decisions.md#2026-06-19--add_edge-in-o1-append--relink--in-place-line-update), [BUG-001 fixed](../legacy/known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco), [BUG-002 fixed](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi).

### `template<class Policy, class NodeFn, class EdgeFn> void Graph::traverse(int start, const std::string& type, NodeFn on_node, EdgeFn on_edge)` (`graph.h:76`)
Generic graph traversal. Maintains `visited` and a `Policy::Frontier`. On visit, calls `on_node(idx)` and pushes to the frontier. On each pop, iterates the node's neighbors for the requested `type`, calls `on_edge(from, to, weight)` for every edge, and visits unvisited targets. Missing node or missing relation → silently skipped.

### `bfs(...)` / `dfs(...)` (`graph.h:115` / `graph.h:121`)
Thin wrappers selecting `BFSPolicy` / `DFSPolicy`.

### I/O helpers (`io/graph_io.h`)

| Function | Purpose |
|---|---|
| `void write_node_index(uint64_t record_offset, uint64_t relation_offset, NodeType, std::ofstream&, const MetaRecord&)` | Builds and writes a `NodeIndex` to `nodes.idx`. `idx.id = meta.next_id`. |
| `void write_complex(const ComplexRecord&, const std::string &json_file_path, std::ostream&)` | Writes the COMPLEX payload to `nodes.dat` (`ComplexHeader` built from current string sizes + two length-prefixed strings: `type_label` and `json_file_path`) and writes `record.json_attributes` to the sidecar file at `JSON_ATTR_PATH / json_file_path`. Takes `std::ostream&` (not `std::ofstream&`) since 2026-06-07 so it can also write **in place** on a `std::fstream` (reuse path). Throws `runtime_error` if the sidecar cannot be opened. |
| `void read_complex(ComplexRecord&, std::ifstream&)` | Reads the COMPLEX payload (header + two length-prefixed strings) from `nodes.dat` and slurps the sidecar JSON file under `JSON_ATTR_PATH` into `out.json_attributes`. Throws `runtime_error` if the sidecar is missing. Called by the `if constexpr` COMPLEX branch of `read_typed_node<T>`. |
| `uint64_t persist_new_edge(MetaRecord&, uint64_t node_id, const std::string& type, uint64_t to_id, uint64_t edge_id, int64_t weight)` | **O(1) add of a new edge (since 2026-06-19).** Reads `relation_offset` from `nodes.idx`, **walks the batch chain** to find the relation line (O(batches) = O(types / 8); the walk stops at the last batch, where a new type goes), allocates the `Edge` slot (pop the 48 B `edges` bin else append to `edges.dat`), writes it with `prev=0, next=` current head, patches the old head's `prev_offset` in place, and updates that one relation line in place — or, for a new relation type, writes a fresh line at slot `type_count` of the **last** batch and bumps `type_count`/`free_bytes` in its header. If that batch is full, since 2026-08-09 it **chains a fresh batch**: allocated from the `rel` bin (else appended), written whole (header `head+1` + the line + zeroed tail), and linked in by patching only the previous batch's `next_offset` (8 bytes). Batches never move → `nodes.idx` untouched in every case. Returns the new edge's `edges.dat` offset. |
| `void persist_edge_weight(uint64_t edge_offset, int64_t weight)` | **O(1) weight overwrite (since 2026-06-19):** seek `edge_offset + offsetof(Edge, weight)` in `edges.dat`, write 8 bytes. No allocation/relink/growth. |
| `void update_node_edges(BaseNode&, MetaRecord&, uint64_t node_id)` | Whole-node rewrite of an already-on-disk node's relations. **Since 2026-06-19 used ONLY by `Graph::delete_node`'s inbound cleanup** (`add_edge` now takes the O(1) `persist_*` path). Step 2 frees the OLD edges by chain-walk (`free_edge_chain`, per-edge 48 B slots onto the `edges` bin, zeroed) and pushes **every batch of the old chain** onto the `rel` bin (offsets collected via `read_relation_node_list`'s out-param; hence the non-`const` `MetaRecord&`). Step 3 writes a fresh chain of `ceil(types / 8)` batches: it first decides where **each** batch lands (one `rel` pop per batch, else successive EOF slots — probing EOF twice without writing would hand back the same offset), then writes them, seeking only at batch boundaries; edge runs are appended fresh at EOF, **refreshing each `EdgeRef.offset`** as it writes (so a later O(1) overwrite seeks correctly after relocation). Step 4 patches `NodeIndex.relation_offset` to the **first** batch in place. |
| `NodeIndex read_node_index(std::ifstream&)` | Reads one `NodeIndex` from the current stream position. |
| `std::vector<RelationEntry> read_relation_node_list(std::ifstream&, std::vector<uint64_t>* batch_offsets = nullptr)` | Reads the whole relation **chain** from the current position: per batch, the `NodeRelationList` header + its `type_count` fixed-width lines (`read_relation_line`), then hops to `next_offset` until 0, bounded by `RELATION_MAX_BATCHES` (a corrupted offset throws instead of looping). Lands at the end of the last batch region. `batch_offsets`, if given, receives the offset of every batch visited — needed by the callers that must reclaim the list, since `NodeIndex` only records where the chain starts. Chain-aware since 2026-08-09. |
| `void write_meta(const MetaRecord&)` | Truncates and rewrites `meta.dat`. |
| `MetaRecord read_meta()` | Reads `meta.dat`. |
| `void write_json_attributes_meta(const JsonMeta&)` | Truncates and rewrites `db/attributes/attributes_meta.dat`. |
| `JsonMeta read_json_attributes_meta()` | Reads the `JsonMeta` POD; lazy-creates the file with `prog_number = 0` if missing. Throws on empty/unreadable file. |
| `template<T> uint64_t write_node_record(const Node<T>&)` | Appends a `NodeRecord<T>` to `nodes.dat`. Returns the offset. |
| `template<T> uint64_t write_relation_node_list(const Node<T>&, uint64_t node_id, std::ofstream& out)` | Appends the node's whole relation chain — `ceil(types / 8)` batches, minimum one — to `out` (already-open `nodes.dat`). Per batch: header (via `relation_batch_header`) + the used fixed-width lines + zero-filled tail (`pad_relation_tail`), full 2213-byte region. Since the batches land back-to-back here, every `next_offset` is known up front (`first + (b+1) * 2213`) and needs no back-patching. Writes each relation's edges to `edges.dat` as a doubly-linked chain (`write_edge_chain_at`). For a fresh node `neighborgs` is empty → one empty batch. Returns the offset of the **first** batch. |
| `template<T> void write_node(const Node<T>&, const MetaRecord&)` | Composes the three writes for a full node persist (record + relations + index). Dispatches the record write via `if constexpr (node_type_of_v<T> == NodeType::COMPLEX)`: primitives go through `node_to_record` + `write_pod`, `COMPLEX` goes through `complex_node_to_record` + `write_complex`. Compiles since 2026-05-30 ([BUG-010](../legacy/known_bugs.md) fixed). |
| `BaseNode* read_node(uint64_t id)` | Reads `NodeIndex` at `id * sizeof(NodeIndex)` in `nodes.idx`, dispatches on `type_id` to the right `read_typed_node<T>` (incl. `COMPLEX` → `read_typed_node<ComplexRecord>`). A `TOMBSTONE` tag throws `"node id N is tombstoned (deleted)"`; an unknown tag throws `"Unknown NodeType"`. |
| `template<T> NodeRecord<T> read_node_record(std::ifstream&)` | Reads one `NodeRecord<T>`. |
| `template<T> BaseNode* read_typed_node(const NodeIndex&, std::ifstream& dat_in)` | Builds a fresh `Node<T>` on the heap with data + neighbors (neighbor pointers left `nullptr`). |
| `void delete_node_from_disk(uint64_t node_id, MetaRecord& meta)` | Orphans the node's regions (NodeRecord, RelationNodeList, each edge chunk) onto the size bins via `write_free_offset`, **zeroes** those byte regions (`zero_region`), **tombstones** the `nodes.idx` slot (`type_id = TOMBSTONE`, offsets zeroed), and updates the counters (`node_count--`, `free_count++`, `free_edge_count += chunks`). `meta` is taken by non-`const` ref since 2026-06-07 (caller does `write_meta`). For COMPLEX: reads the on-disk `ComplexHeader` for the real record size → `complex_<size>` bin, `remove()`s the JSON sidecar, and recycles its `prog_number` onto the json free list. Throws `runtime_error` on file-open failure. |
| `template<T> void write_node_in_freed_slot(const Node<T>&, uint64_t node_id, uint64_t record_offset)` | Reuse-path counterpart of `write_node`: writes `NodeRecord<T>` **in place** at the freed `record_offset` in `nodes.dat`, the `NodeIndex` **in place** at the freed id slot in `nodes.idx`, and **appends** the (empty) `NodeRelationList` batch at end-of-`nodes.dat`. Never instantiated for COMPLEX (variable on-disk size). |
| `void write_complex_in_freed_slot(const Node<ComplexRecord>&, uint64_t node_id, uint64_t record_offset)` (inline) | COMPLEX counterpart of `write_node_in_freed_slot`. Writes `ComplexHeader` + two strings **in place** at the freed `record_offset` (exact-fit slot) via `complex_node_to_record` + `write_complex` (which also writes the sidecar and assigns the `prog_number`), appends the empty `NodeRelationList` batch, and writes the `NodeIndex` (`type_id = COMPLEX`) in place. Added 2026-06-07. |
| `std::unordered_map<int, std::unordered_set<int>> build_inbound_index(uint64_t next_id)` | Scans every live node (skips `TOMBSTONE`) and follows its relation chunks to build the reverse index `to_node → {from_node}`. O(N+E). Only live nodes' chunks are read, so zeroed/freed edges are never counted. Used by `Graph::build_in_edges` at load. Added 2026-06-07. |
| `uint64_t complex_record_on_disk_size(uint64_t type_label_len)` (inline) | On-disk size of a COMPLEX record as a pure function of `type_label` length: `sizeof(ComplexHeader) + L + (COMPLEX_PROG_DIGITS + 1 + L + 5)`. Used to pick the `complex_<size>` bin on insert. Added 2026-06-07. |
| `std::filesystem::path json_freelist_path()` (inline) | Path of the json free list `db/freelist/json_prog.dat` (LIFO stack of freed `prog_number`s). Added 2026-06-07. |
| `std::filesystem::path freelist_bin_path(const std::string& prefix, uint64_t size)` (inline) | Builds `db/freelist/<prefix>_<size>.dat`. `prefix ∈ {"nodes","rel","edges","complex"}`. |
| `template<T> void write_free_offset(const T& fo, const std::filesystem::path&)` | Push: appends one free-offset record to its size bin (O(1)). Creates `db/freelist/` on first use. Throws on open failure. |
| `template<T> std::optional<T> pop_free_offset(const std::filesystem::path&)` | Pop (LIFO): reads the last record then truncates the file by one record (O(1), exact fit). `std::nullopt` if the bin is missing/empty; throws if the truncate (`resize_file`) fails. |

### POD helpers (`io/io_utils.h`)

| Function | Purpose |
|---|---|
| `template<T> void write_pod(const T&, std::ostream&)` | Writes `sizeof(T)` raw bytes. `static_assert` on `is_trivially_copyable_v<T>`. |
| `template<T> T read_pod(std::ifstream&)` | Reads `sizeof(T)` raw bytes. Same assert. |
| `void write_string(const std::string&, std::ostream&)` | Writes `uint64_t length` followed by raw bytes. |
| `std::string read_string(std::ifstream&)` | Reads length-prefixed string. |
| `void write_offset(uint64_t, std::ostream&)` | Writes a bare `uint64_t`. |
| `uint64_t read_offset(std::ifstream&)` | Reads a bare `uint64_t`. |

### ODT helpers (`odt/`)

| Function | Purpose |
|---|---|
| `template<T> NodeRecord<T> node_to_record(const Node<T>&)` (`node_odt.h:22`) | Copies `node.data` into a `NodeRecord<T>`. Asserts POD. |
| `NodeRecord<ComplexHeader> complex_node_to_record(const Node<ComplexRecord>&, std::string &json_file_path)` (`node_odt.cpp`) | COMPLEX-specific ODT bridge. Decides the `prog_number`: pops a recycled one from the json free list, else consumes `JsonMeta.prog_number` and persists `+1` (BUG-014 fix). Composes the out-param `json_file_path` as `{prog_number:020}_{type_label}.json` — **zero-padded to `COMPLEX_PROG_DIGITS`** so the record size is constant per type. Builds a `ComplexHeader` from `type_label.size()` and `json_file_path.size()` and returns it wrapped in `NodeRecord<ComplexHeader>` (currently unused by `write_node`, which rebuilds the header inside `write_complex` from the same inputs). |
| `NodeRelationList relation_batch_header(uint64_t node_id, uint64_t lines_in_batch, uint64_t head = 1, uint64_t next_offset = 0)` (`node_odt.cpp`) | Builds the header of **one** batch of the chain: `node_id`, `type_count = lines_in_batch`, `batch_size = RELATION_BATCH_TAIL` (constant), `free_bytes = batch_size - lines_in_batch*272`, `next_offset`, `head`, `is_deleted = 0`. Throws `std::invalid_argument` if `lines_in_batch > RELATION_LINES_PER_BATCH` (a caller bug: the overflow belongs in the next batch). The tail lines are written separately by `write_relation_node_list` / `update_node_edges`, which are also the ones that know the chain layout. Replaced `node_to_relation_list(const BaseNode&, node_id, head)` on 2026-08-09 — see [API change](../legacy/api_changes.md#2026-08-09--firme-relation-batch-relation_batch_header-read_relation_node_list-con-batch_offsets). |
| `NodeIndex node_to_node_index(uint64_t id, uint64_t record_offset, uint64_t relation_offset)` (`node_odt.cpp`) | Builder for `NodeIndex` (currently unused — `write_node_index` builds the struct inline). |
| `Edge edge_to_pod(uint64_t idx, uint64_t from, uint64_t to, uint64_t weight, uint64_t prev_offset = 0, uint64_t next_offset = 0)` (`edge_odt.h`) | Plain struct-builder for `Edge`. Gained the two optional chain offsets on 2026-06-19. |

`reconstruct_neighbors` and `node_form_pod` were **removed** as dead code 2026-06-07 ([BUG-003](../legacy/known_bugs.md#2026-05-26--bug-003-reconstruct_neighbors-non-implementata) / [BUG-004](../legacy/known_bugs.md#2026-05-26--bug-004-typo-neihborgs-in-node_form_pod)): no callers, and their signatures had no access to the on-disk tail or `edges.dat`. The real POD→domain reconstruction is `read_typed_node` (`io/graph_io.h`).

## Diagrammi

### Layering inside graph_core

```
        ┌──────────────────────┐
        │      graph.h/cpp     │  Graph class (public surface)
        └──────────┬───────────┘
                   │
       ┌───────────┼───────────┐
       │           │           │
       ▼           ▼           ▼
┌───────────┐ ┌───────┐ ┌──────────┐
│  struct/  │ │  odt/ │ │   io/    │
│ (domain   │ │ (D↔P) │ │ (binary  │
│  + POD)   │ │       │ │   I/O)   │
└─────┬─────┘ └───┬───┘ └────┬─────┘
      │           │          │
      └───────────┴──────────┘
                  │
                  ▼
            ┌─────────┐
            │  db/    │
            └─────────┘
```

### Node layout on disk (for one inserted node)

```
nodes.idx                       nodes.dat                                edges.dat
┌──────────────┐                ┌─────────────────────────────┐         ┌──────────────┐
│ NodeIndex #0 │ ─ offset ───▶  │ NodeRecord<T> for node 0    │         │ Edge (head)  │◀┐
├──────────────┤                ├─────────────────────────────┤   ┌──▶  │   next_off ─▶│ │
│ NodeIndex #1 │ ─ relation_    │ NodeRelationList header(37) │   │     │ Edge         │ │ chain per
├──────────────┤   offset ──▶   │ line0: edge_off│count│name  │ ──┘     │   prev_off ──┼─┘ (node,rel)
│ ...          │                │ line1..7 (fixed 272, padded)│         │ ...          │
└──────────────┘                │ NodeRecord<T> for node 1    │         └──────────────┘
                                │ ...                         │  batch = 2213 bytes (fixed)
                                └─────────────────────────────┘  edge_off → chain head
```

A node with more than 8 relation types owns a **chain** of batches (`relation_offset` → the
first, `next_offset` → the following one). The batches are allocated independently, so they
are not necessarily adjacent in `nodes.dat`:

```
nodes.idx                nodes.dat (offsets not necessarily in order)
┌──────────────┐         ┌───────────────────────────────┐
│ NodeIndex #8 │         │ batch head=1  type_count=8     │  lines rel_0..rel_7
│  relation_   │ ──────▶ │  next_offset ──────────────┐   │
│  offset      │         ├────────────────────────────┼───┤
└──────────────┘         │ ...other nodes' regions... │   │
                         ├────────────────────────────▼───┤
                         │ batch head=2  type_count=8     │  lines rel_8..rel_15
                         │  next_offset ──────────────┐   │
                         ├────────────────────────────┼───┤
                         │ ...                        │   │
                         ├────────────────────────────▼───┤
                         │ batch head=3  type_count=1     │  line rel_16
                         │  next_offset = 0  (last)       │  + 7 zeroed lines
                         └───────────────────────────────┘
```

## Dipendenze

**IN** (who depends on `graph_core`):
- `main.cpp` (via `#include "graph_core/graph.h"`).

**OUT** (what `graph_core` depends on):
- C++ standard library only (`<unordered_map>`, `<unordered_set>`, `<vector>`, `<string>`, `<stdexcept>`, `<filesystem>`, `<fstream>`, `<queue>`, `<stack>`, `<type_traits>`, `<optional>`, `<cstddef>`, `<ctime>`).

No third-party libraries. No dependency on `data_tructures/`.

## Voci legacy collegate

- [Relation-batch chaining: catena di batch via next_offset](../legacy/design_decisions.md#2026-08-09--relation-batch-chaining-catena-di-batch-via-next_offset)
- [API — Firme relation-batch: relation_batch_header, read_relation_node_list con batch_offsets](../legacy/api_changes.md#2026-08-09--firme-relation-batch-relation_batch_header-read_relation_node_list-con-batch_offsets)
- [add_edge in O(1): append + relink + in-place line update](../legacy/design_decisions.md#2026-06-19--add_edge-in-o1-append--relink--in-place-line-update)
- [API — add_edge O(1): EdgeRef.offset + persist_new_edge / persist_edge_weight](../legacy/api_changes.md#2026-06-19--add_edge-o1-edgerefoffset--persist_new_edge--persist_edge_weight)
- [Relation-list a batch fixed-width + Edge a lista doppiamente concatenata](../legacy/design_decisions.md#2026-06-19--relation-list-a-batch-fixed-width--edge-a-lista-doppiamente-concatenata)
- [API — RelationNodeList → NodeRelationList: header esteso + tail fixed-width](../legacy/api_changes.md#2026-06-19--relationnodelist--noderelationlist-header-esteso--tail-fixed-width)
- [API — Edge: prev_offset / next_offset](../legacy/api_changes.md#2026-06-19--edge-aggiunti-prev_offset--next_offset-32--48-byte)
- [API — Firme ODT: edge_to_pod / node_to_relation_list](../legacy/api_changes.md#2026-06-19--firme-odt-edge_to_pod-prevnext-node_to_relation_list-node_id-head)
- [Reuse of the rel/edges freelist bins (edge-space compaction)](../legacy/design_decisions.md#2026-06-13--reuse-of-the-reledges-freelist-bins-edge-space-compaction)
- [API — update_node_edges step 3: append-only → pop-then-append](../legacy/api_changes.md#2026-06-13--update_node_edges-step-3-append-only--pop-then-append)
- [BUG-017 — update_node_edges freelist push/reuse](../legacy/known_bugs.md#2026-06-07--bug-017-update_node_edges-orfanizza-regioni-senza-spingerle-sulla-freelist)
- [Bin per-tipo per i record COMPLEX via prog_number zero-paddato](../legacy/design_decisions.md#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato)
- [Indice inverso degli archi entranti in-RAM](../legacy/design_decisions.md#2026-06-07--indice-inverso-degli-archi-entranti-in-ram)
- [Tombstone + azzeramento delle regioni su delete](../legacy/design_decisions.md#2026-06-07--tombstone--azzeramento-delle-regioni-su-delete)
- [API — NodeType::TOMBSTONE](../legacy/api_changes.md#2026-06-07--nodetypetombstone-aggiunto)
- [API — delete_node_from_disk MetaRecord& + build_inbound_index](../legacy/api_changes.md#2026-06-07--delete_node_from_disk-const-metarecord--metarecord-build_inbound_index)
- [API — Graph in_edges + delete_node completata](../legacy/api_changes.md#2026-06-07--graph-membro-in_edges--build_in_edges-delete_node-completata)
- [API — write_complex su std::ostream& + write_complex_in_freed_slot](../legacy/api_changes.md#2026-06-07--write_complex-su-stdostream--write_complex_in_freed_slot)
- [API — complex_node_to_record prog_number + zero-pad](../legacy/api_changes.md#2026-06-07--complex_node_to_record-prog_number-incrementatopersistito--zero-pad-complex_prog_digits)
- [BUG-014 — prog_number non incrementato (fixed)](../legacy/known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex)
- [Freelist a bin segregati per dimensione esatta + cancellazione nodo](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo)
- [API — FreeRecord rimossa, tre POD free-offset](../legacy/api_changes.md#2026-06-03--freerecord-rimossa-sostituita-da-tre-pod-free-offset)
- [API — Graph::delete_node](../legacy/api_changes.md#2026-06-03--nuova-graphdelete_nodeint)
- [API — Graph::insert reuse path](../legacy/api_changes.md#2026-06-03--graphinsertt-aggiunto-il-reuse-path-via-freelist)
- [API — funzioni I/O freelist + delete_node_from_disk](../legacy/api_changes.md#2026-06-03--nuove-funzioni-io-freelist--delete_node_from_disk)
- [API — node_record_payload_size + type_registry.cpp](../legacy/api_changes.md#2026-06-03--nuovo-node_record_payload_size--tu-type_registrycpp)
- [BUG-016 — delete_node prototipo incompleto](../legacy/known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex)
- [Edge persistence: append + obsolete + in-place index patch](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch)
- [Storage sidecar JSON per nodi COMPLEX](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex)
- [Introduzione tag NodeType::COMPLEX + ComplexRecord (WIP)](../legacy/design_decisions.md#2026-05-26--introduzione-tag-nodetypecomplex--complexrecord-wip)
- [Separazione POD vs Domain struct](../legacy/design_decisions.md#2026-05-26--separazione-pod-vs-domain-struct)
- [Type-erased BaseNode + Node&lt;T&gt;](../legacy/design_decisions.md#2026-05-26--type-erased-basenode--nodet)
- [Policy-based traversal](../legacy/design_decisions.md#2026-05-26--policy-based-traversal-bfsdfs)
- [Append-only data files, truncated meta](../legacy/design_decisions.md#2026-05-26--append-only-data-files-truncated-meta)
- [Single-open append su nodes.dat](../legacy/design_decisions.md#2026-05-26--single-open-append-su-nodesdat)
- [API — RelationNodeList con batch_size](../legacy/api_changes.md#2026-05-30--relationnodelist-aggiunto-il-campo-batch_size)
- [API — update_node_edges nuova funzione](../legacy/api_changes.md#2026-05-30--nuova-funzione-update_node_edges)
- [API — Graph::add_edge persiste](../legacy/api_changes.md#2026-05-30--graphadd_edge-persistenza-su-disco-via-update_node_edges)
- [API — MetaRecord campi edge](../legacy/api_changes.md#2026-06-02--metarecord-aggiunti-i-campi-edge_count-next_edge_id-free_edge_count)
- [API — neighborgs: EdgeRef](../legacy/api_changes.md#2026-06-02--basenodeneighborgs-da-pairint-basenode-a-edgeref)
- [Decisione — id arco in EdgeRef](../legacy/design_decisions.md#2026-06-02--id-arco-globale-sorgente-in-metarecordnext_edge_id-memorizzato-in-edgeref)
- [API — ComplexHeader rinominato](../legacy/api_changes.md#2026-05-26--complexheaderjson_attributes_size--json_file_path_size)
- [API — write_node switch su NodeType](../legacy/api_changes.md#2026-05-26--write_nodet-switch-su-nodetype-per-ramo-complex)
- [BUG-001 — add_edge non persiste (fixed)](../legacy/known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco)
- [BUG-002 — Edge.id non globale (fixed)](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi)
- [BUG-003 — reconstruct_neighbors stub](../legacy/known_bugs.md#2026-05-26--bug-003-reconstruct_neighbors-non-implementata)
- [BUG-004 — typo neihborgs](../legacy/known_bugs.md#2026-05-26--bug-004-typo-neihborgs-in-node_form_pod)
- [BUG-005 — logger globale duplicato](../legacy/known_bugs.md#2026-05-26--bug-005-logger-globale-duplicato-in-graph_iocpp)
- [BUG-009 — write_complex definita due volte](../legacy/known_bugs.md#2026-05-26--bug-009-write_complex-definita-due-volte-in-graph_iocpp)
- [BUG-010 — case COMPLEX di write_node non compila](../legacy/known_bugs.md#2026-05-26--bug-010-ramo-case-nodetypecomplex-di-write_node-non-compila)
- [BUG-011 — complex_node_to_record concatena uint64_t + const char*](../legacy/known_bugs.md#2026-05-26--bug-011-complex_node_to_record-concatena-uint64_t--const-char)
- [BUG-012 — logger globale in node_odt.cpp](../legacy/known_bugs.md#2026-05-26--bug-012-logger-globale-duplicato-in-node_odtcpp)
- [BUG-013 — path sidecar incoerente](../legacy/known_bugs.md#2026-05-26--bug-013-path-del-file-json-sidecar-incoerente-tra-complex_node_to_record-e-write_complex)
- [BUG-014 — prog_number non incrementato](../legacy/known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex)

## Riferimenti

- `graph_core/graph.h:19` — `class Graph`.
- `graph_core/graph.h:43` — `insert<T>` template.
- `graph_core/graph.h:76` — `traverse<Policy, ...>` template.
- `graph_core/graph.cpp:57` — `add_edge` (resolves edge id, bumps `next_edge_id`/`edge_count`).
- `graph_core/struct/domain_struct.h:24` — `EdgeRef` (RAM-side edge: `id`, `weight`, `neighbor`, `offset`).
- `graph_core/io/graph_io.h` / `graph_core/io/graph_io.cpp` — `persist_new_edge`, `persist_edge_weight` (O(1) add/overwrite), `free_edge_chain` (per-edge reclaim).
- `graph_core/struct/domain_struct.h:35` — `BaseNode` (`neighborgs` value type now `EdgeRef`).
- `graph_core/struct/domain_struct.h:58` — `ComplexRecord`.
- `graph_core/struct/pod_struct.h:136` — `MetaRecord` (48 bytes: 3 node + 3 edge counters).
- `graph_core/struct/pod_struct.h:182,196,209` — `NodeFreeOffset`, `RelationNodeListFreeOffset`, `BatchOfEdgesFreeOffset` (freelist free-offset PODs; replaced `FreeRecord`).
- `graph_core/struct/pod_struct.h:16` — `NodeType` (incl. `COMPLEX = 255`).
- `graph_core/struct/pod_struct.h:41` — `NodeIndex`.
- `graph_core/struct/pod_struct.h` — `NodeRelationList` (37-byte header + fixed 2176-byte tail = 2213-byte batch; renamed from `RelationNodeList` 2026-06-19).
- `graph_core/struct/pod_struct.h` — `Edge` (48 bytes: `id`, `weight`, `to_node`, `from_node`, `prev_offset`, `next_offset`).
- `graph_core/costants.h` — `RELATION_NAME_MAX`, `RELATION_LINE_SIZE`, `RELATION_LINES_PER_BATCH`, `RELATION_BATCH_TAIL`, `RELATION_MAX_BATCHES`.
- `graph_core/struct/pod_struct.h:134` — `ComplexHeader` (field `json_file_path_size`).
- `graph_core/struct/pod_struct.h:148` — `JsonMeta`.
- `graph_core/struct/functions_policies.h:12` — `BFSPolicy`.
- `graph_core/struct/type_registry.h:33` — `node_type_of<ComplexRecord> → COMPLEX`.
- `graph_core/costants.h:7-11` — `DB_PATH`, `META_FILE_PATH`, `JSON_ATTR_META_PATH`, `JSON_ATTR_PATH`.
- `graph_core/io/graph_io.h:24` — `write_complex` declaration.
- `graph_core/io/graph_io.h:38` — `read_complex` declaration.
- `graph_core/io/graph_io.h:62` — `update_node_edges` declaration.
- `graph_core/io/graph_io.h:107` — `write_node` template (`if constexpr` dispatch on `NodeType`).
- `graph_core/io/graph_io.cpp:34` — `write_complex`.
- `graph_core/io/graph_io.cpp:65` — `read_complex`.
- `graph_core/io/graph_io.cpp:120` — `read_node` (type dispatch — COMPLEX case routes to `read_typed_node<ComplexRecord>`).
- `graph_core/io/graph_io.cpp:172,190` — `write_json_attributes_meta`, `read_json_attributes_meta`.
- `graph_core/io/graph_io.cpp:247` — `update_node_edges`.
- `graph_core/odt/node_odt.cpp:32` — `relation_batch_header` (per-batch header of the chain; replaced `node_to_relation_list` 2026-08-09).
- `graph_core/io/graph_io.h:146` — `relation_batch_region_size` / `relation_batch_count` / `relation_lines_in_batch` (chain layout helpers).
- `graph_core/io/graph_io.cpp:164` — `read_relation_node_list` (walks `next_offset`, optional `batch_offsets` out-param).
- `graph_core/io/graph_io.cpp:472` — `persist_new_edge`, chained-batch branch (allocate + link via one 8-byte `next_offset` write).
- `graph_core/odt/node_odt.cpp:55` — `complex_node_to_record`.
- `graph_core/graph.h:43` — `insert<T>` reuse path (`pop_free_offset` + `write_node_in_freed_slot`).
- `graph_core/graph.h:117` — `delete_node` declaration.
- `graph_core/graph.cpp:163` — `delete_node` (RAM removal + `delete_node_from_disk`).
- `graph_core/io/graph_io.h:65` — `delete_node_from_disk` declaration.
- `graph_core/io/graph_io.h:190` — `write_node_in_freed_slot` template.
- `graph_core/io/graph_io.h:321,333,355` — `freelist_bin_path`, `write_free_offset`, `pop_free_offset`.
- `graph_core/io/graph_io.cpp:380` — `delete_node_from_disk`.
- `graph_core/struct/type_registry.h:51` — `node_record_payload_size` declaration.
- `graph_core/struct/type_registry.cpp` — `node_record_payload_size` definition (out-of-line).
