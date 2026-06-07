# Module: graph_core

> The C++17 core of pointer_graphs. Defines the `Graph` class, the dual POD/domain struct hierarchy, the ODT translation layer, and the binary I/O layer.

| Campo | Valore |
|---|---|
| Tipo | module |
| Lingua | en |
| Ultimo aggiornamento | 2026-06-07 |
| Commit di riferimento | 9966603 |
| Mirror | — |

---

## Overview

`graph_core` is the in-process graph engine. It keeps nodes in a `std::unordered_map<int, BaseNode*>` for O(1) random access by id, persists every insert immediately to the `db/` files, and exposes generic BFS/DFS via a policy-based traversal template.

## Struttura

```
graph_core/
├── graph.h                     Graph class (header-only templates + declarations)
├── graph.cpp                   Graph non-template implementation
├── costants.h                  Compile-time constants (RELATION_TYPE_MAX_SIZE, DB_PATH)
├── logger.h                    Header-only file logger
├── struct/
│   ├── domain_struct.h         BaseNode (type-erased) + Node<T> (typed payload)
│   ├── pod_struct.h            On-disk POD layouts (packed)
│   ├── functions_policies.h    BFSPolicy / DFSPolicy
│   ├── type_registry.h         Compile-time C++ type ↔ NodeType mapping (+ node_record_payload_size decl)
│   └── type_registry.cpp       node_record_payload_size (out-of-line, non-template)
├── odt/
│   ├── node_odt.h/cpp          Domain Node<T> ↔ POD NodeRecord/RelationNodeList/NodeIndex
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
| `relation_offset` | `uint64_t` | Byte offset into `nodes.dat` where the `RelationNodeList` for this node starts. |
| `type_id` | `NodeType` | Dispatch tag used by `read_node` to pick the right `read_typed_node<T>`. |

### `template<class T> struct NodeRecord` (POD, packed) (`struct/pod_struct.h`)
Just `T data;`. `T` must be trivially copyable (asserted at use site).

### `struct RelationNodeList` (POD, packed) (`struct/pod_struct.h`)
Header for the adjacency list of a node. 16 bytes since 2026-05-30 (was 8 — see [API change](../legacy/api_changes.md#2026-05-30--relationnodelist-aggiunto-il-campo-batch_size)).
| Field | Type | Purpose |
|---|---|---|
| `type_count` | `uint64_t` | Number of distinct relation types this node has. |
| `batch_size` | `uint64_t` | Size in bytes of the variable-width tail that follows the POD (does NOT include the 16 bytes of the POD itself). Two uses: read the tail in one shot, and record the reclaimable size when the region is orphaned by `update_node_edges`. |

Per-relation entries `[uint64_t name_length][name bytes][uint64_t edge_offset][uint64_t edge_count]` are written **immediately after** the POD by `write_relation_node_list` (on insert) or `update_node_edges` (on edge update). The variable-width tail is not part of the struct itself; its total size equals `batch_size`.

### `struct Edge` (POD, packed) (`struct/pod_struct.h`)
| Field | Type | Purpose |
|---|---|---|
| `id` | `uint64_t` | Globally-unique edge id, sourced from `MetaRecord.next_edge_id` (since 2026-06-02 — [BUG-002](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi) fixed). Written from `EdgeRef.id`. |
| `weight` | `int64_t` | Edge weight. |
| `to_node` | `uint64_t` | Destination node id. |
| `from_node` | `uint64_t` | Source node id. |

### `struct MetaRecord` (POD, packed) (`struct/pod_struct.h`)
| Field | Type | Purpose |
|---|---|---|
| `next_id` | `uint64_t` | Next node id to assign on insert. |
| `node_count` | `uint64_t` | Total nodes ever inserted. |
| `free_count` | `uint64_t` | Count of free node slots across all freelist bins. `delete_node_from_disk` increments it (+1), the reuse path of `Graph::insert` decrements it (−1). Maintained since 2026-06-07. |
| `edge_count` | `uint64_t` | Number of live edges. Incremented by `add_edge` for each genuinely new edge (since 2026-06-02). |
| `next_edge_id` | `uint64_t` | Next edge id to assign. Monotonic source for `Edge.id` / `EdgeRef.id` (since 2026-06-02 — [BUG-002](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi) fixed). |
| `free_edge_count` | `uint64_t` | Count of freed edge chunks (one freelist record per orphaned `(node, relation)` chunk). `delete_node_from_disk` adds the deleted node's chunk count. Maintained since 2026-06-07. Caveat: the chunks orphaned by `update_node_edges` (on `add_edge` and on inbound-edge cleanup) are still only logged, not pushed — so they are NOT counted here. |

POD layout grew from 24 to 48 bytes on 2026-06-02 (the three edge fields) — see [API change](../legacy/api_changes.md#2026-06-02--metarecord-aggiunti-i-campi-edge_count-next_edge_id-free_edge_count). Any `db/meta.dat` produced before that must be wiped.

### Freelist free-offset PODs (POD, packed) (`struct/pod_struct.h`)
Three records describing a reclaimable on-disk region, pushed onto the size-segregated freelist bins under `db/freelist/`. They replace the old single-field `FreeRecord` (removed 2026-06-03 — see [API change](../legacy/api_changes.md#2026-06-03--freerecord-rimossa-sostituita-da-tre-pod-free-offset)). All packed. Written/read via the `write_free_offset` / `pop_free_offset` templates. See the [freelist design decision](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo).

**`struct NodeFreeOffset`** — a freed `NodeRecord` region in `nodes.dat` + the now-reusable id slot in `nodes.idx`.
| Field | Type | Purpose |
|---|---|---|
| `idx` | `uint64_t` | Reusable node id (its fixed-width slot in `nodes.idx`). |
| `offset` | `uint64_t` | Start of the orphaned `NodeRecord` in `nodes.dat`. |
| `size` | `uint64_t` | Byte size of the free region (selects the bin). |

**`struct RelationNodeListFreeOffset`** — a freed `RelationNodeList` region (header + variable-width tail) in `nodes.dat`. No id: a relation list has no standalone id, only its byte region.
| Field | Type | Purpose |
|---|---|---|
| `offset` | `uint64_t` | Start of the orphaned `RelationNodeList` in `nodes.dat`. |
| `size` | `uint64_t` | `sizeof(RelationNodeList) + batch_size`. |

**`struct BatchOfEdgesFreeOffset`** — a freed contiguous chunk of `Edge` records in `edges.dat` (all edges of one `(node, relation)` pair).
| Field | Type | Purpose |
|---|---|---|
| `idx` | `uint64_t` | Id of the first edge of the batch (reusable edge-id starting point). |
| `offset` | `uint64_t` | Start of the orphaned chunk in `edges.dat`. |
| `size` | `uint64_t` | `edge_count * sizeof(Edge)`. |

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
In-memory form of one relation-type entry parsed from `RelationNodeList` tail. Fields: `std::string name; uint64_t edge_offset; uint64_t edge_count;`.

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
3. Resolves the edge id and whether it is new: a brand-new `(start, type, end)` triple consumes a fresh id from `meta.next_edge_id`; an existing triple reuses the id already stored in its `EdgeRef` (only the weight is overwritten). Decided **before** the mutation in step 4.
4. Inserts `EdgeRef{id, weight, end_ptr}` into `start->neighborgs[type][end]` (RAM-first).
5. Calls `update_node_edges(*start_node, meta, start)` to persist the new state — runs on every call, even for an overwrite.
6. Only for a genuinely new edge: increments `meta.next_edge_id` and `meta.edge_count`, then `write_meta(meta)`.

**Side effects on disk (since 2026-05-30):** appends a new `RelationNodeList` to `nodes.dat`, appends a fresh contiguous chunk per relation to `edges.dat`, patches `NodeIndex.relation_offset` in `nodes.idx` in place. Since 2026-06-02 a new edge also truncates+rewrites `meta.dat` (counter bump). The previously persisted `RelationNodeList` region and the previously persisted edge chunks become orphaned bytes (no freelist persistence yet). See [BUG-001 fixed](../legacy/known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco), [BUG-002 fixed](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi) and the [Edge persistence design decision](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch).

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
| `void update_node_edges(BaseNode&, const MetaRecord&, uint64_t node_id)` | Persists the current `neighborgs` of an already-on-disk node to `nodes.dat` / `edges.dat` and patches `NodeIndex.relation_offset` in `nodes.idx` in place. Reads the OLD relation list to compute the orphaned regions and logs them on `graph_io.log` (placeholder for the future freelist). Single source of in-place mutation in the system; called by `Graph::add_edge` since 2026-05-30. The `MetaRecord&` parameter is currently unused — reserved for when the freelist becomes persistent. |
| `NodeIndex read_node_index(std::ifstream&)` | Reads one `NodeIndex` from the current stream position. |
| `std::vector<RelationEntry> read_relation_node_list(std::ifstream&)` | Reads the `RelationNodeList` header and its variable-width tail. |
| `void write_meta(const MetaRecord&)` | Truncates and rewrites `meta.dat`. |
| `MetaRecord read_meta()` | Reads `meta.dat`. |
| `void write_json_attributes_meta(const JsonMeta&)` | Truncates and rewrites `db/attributes/attributes_meta.dat`. |
| `JsonMeta read_json_attributes_meta()` | Reads the `JsonMeta` POD; lazy-creates the file with `prog_number = 0` if missing. Throws on empty/unreadable file. |
| `template<T> uint64_t write_node_record(const Node<T>&)` | Appends a `NodeRecord<T>` to `nodes.dat`. Returns the offset. |
| `template<T> uint64_t write_relation_node_list(const Node<T>&, uint64_t node_id, std::ofstream& out)` | Appends `RelationNodeList` header (with `batch_size` populated via `node_to_relation_list`) + tail to `out` (already-open `nodes.dat`), and appends each edge to `edges.dat`. Returns the byte offset where the relation list was written. |
| `template<T> void write_node(const Node<T>&, const MetaRecord&)` | Composes the three writes for a full node persist (record + relations + index). Dispatches the record write via `if constexpr (node_type_of_v<T> == NodeType::COMPLEX)`: primitives go through `node_to_record` + `write_pod`, `COMPLEX` goes through `complex_node_to_record` + `write_complex`. Compiles since 2026-05-30 ([BUG-010](../legacy/known_bugs.md) fixed). |
| `BaseNode* read_node(uint64_t id)` | Reads `NodeIndex` at `id * sizeof(NodeIndex)` in `nodes.idx`, dispatches on `type_id` to the right `read_typed_node<T>` (incl. `COMPLEX` → `read_typed_node<ComplexRecord>`). A `TOMBSTONE` tag throws `"node id N is tombstoned (deleted)"`; an unknown tag throws `"Unknown NodeType"`. |
| `template<T> NodeRecord<T> read_node_record(std::ifstream&)` | Reads one `NodeRecord<T>`. |
| `template<T> BaseNode* read_typed_node(const NodeIndex&, std::ifstream& dat_in)` | Builds a fresh `Node<T>` on the heap with data + neighbors (neighbor pointers left `nullptr`). |
| `void delete_node_from_disk(uint64_t node_id, MetaRecord& meta)` | Orphans the node's regions (NodeRecord, RelationNodeList, each edge chunk) onto the size bins via `write_free_offset`, **zeroes** those byte regions (`zero_region`), **tombstones** the `nodes.idx` slot (`type_id = TOMBSTONE`, offsets zeroed), and updates the counters (`node_count--`, `free_count++`, `free_edge_count += chunks`). `meta` is taken by non-`const` ref since 2026-06-07 (caller does `write_meta`). For COMPLEX: reads the on-disk `ComplexHeader` for the real record size → `complex_<size>` bin, `remove()`s the JSON sidecar, and recycles its `prog_number` onto the json free list. Throws `runtime_error` on file-open failure. |
| `template<T> void write_node_in_freed_slot(const Node<T>&, uint64_t node_id, uint64_t record_offset)` | Reuse-path counterpart of `write_node`: writes `NodeRecord<T>` **in place** at the freed `record_offset` in `nodes.dat`, the `NodeIndex` **in place** at the freed id slot in `nodes.idx`, and **appends** the (empty) `RelationNodeList` at end-of-`nodes.dat`. Never instantiated for COMPLEX (variable on-disk size). |
| `void write_complex_in_freed_slot(const Node<ComplexRecord>&, uint64_t node_id, uint64_t record_offset)` (inline) | COMPLEX counterpart of `write_node_in_freed_slot`. Writes `ComplexHeader` + two strings **in place** at the freed `record_offset` (exact-fit slot) via `complex_node_to_record` + `write_complex` (which also writes the sidecar and assigns the `prog_number`), appends the empty `RelationNodeList`, and writes the `NodeIndex` (`type_id = COMPLEX`) in place. Added 2026-06-07. |
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
| `RelationNodeList node_to_relation_list(const BaseNode&)` (`node_odt.cpp:27`) | Fills `type_count` and `batch_size`. `batch_size` is computed as `Σ (3 * sizeof(uint64_t) + name.size())` over all relations in `node.neighborgs` — see [API change](../legacy/api_changes.md#2026-05-30--relationnodelist-aggiunto-il-campo-batch_size). The variable-width tail itself is written separately by `write_relation_node_list` (on insert) or by `update_node_edges` (on edge update). |
| `NodeIndex node_to_node_index(uint64_t id, uint64_t record_offset, uint64_t relation_offset)` (`node_odt.cpp:39`) | Builder for `NodeIndex` (currently unused — `write_node_index` builds the struct inline). |
| `std::unordered_map<...> reconstruct_neighbors(const RelationNodeList&)` (`node_odt.cpp:86`) | **Stub — returns an empty map.** See [BUG-003](../legacy/known_bugs.md). |
| `template<T> BaseNode node_form_pod(const NodeIndex&, const NodeRecord<T>&, const RelationNodeList&)` (`node_odt.h:63`) | **Currently broken** — references `node.neihborgs` (typo, missing `b`). See [BUG-004](../legacy/known_bugs.md). |
| `Edge edge_to_pod(uint64_t idx, uint64_t from, uint64_t to, uint64_t weight)` (`edge_odt.h:17`) | Plain struct-builder for `Edge`. |

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
nodes.idx                       nodes.dat                              edges.dat
┌──────────────┐                ┌───────────────────────────┐         ┌──────────┐
│ NodeIndex #0 │ ─ offset ───▶  │ NodeRecord<T> for node 0  │         │ Edge     │
├──────────────┤                ├───────────────────────────┤   ┌──▶  │ Edge     │
│ NodeIndex #1 │ ─ relation_    │ RelationNodeList header   │   │     │ ...      │
├──────────────┤   offset ──▶   │ [name][edge_off][count]   │ ──┘     └──────────┘
│ ...          │                │ [name][edge_off][count]   │
└──────────────┘                │ ...                       │
                                │ NodeRecord<T> for node 1  │
                                │ ...                       │
                                └───────────────────────────┘
```

## Dipendenze

**IN** (who depends on `graph_core`):
- `main.cpp` (via `#include "graph_core/graph.h"`).

**OUT** (what `graph_core` depends on):
- C++ standard library only (`<unordered_map>`, `<unordered_set>`, `<vector>`, `<string>`, `<stdexcept>`, `<filesystem>`, `<fstream>`, `<queue>`, `<stack>`, `<type_traits>`, `<optional>`, `<cstddef>`, `<ctime>`).

No third-party libraries. No dependency on `data_tructures/`.

## Voci legacy collegate

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
- `graph_core/struct/domain_struct.h:24` — `EdgeRef` (RAM-side edge: `id`, `weight`, `neighbor`).
- `graph_core/struct/domain_struct.h:35` — `BaseNode` (`neighborgs` value type now `EdgeRef`).
- `graph_core/struct/domain_struct.h:58` — `ComplexRecord`.
- `graph_core/struct/pod_struct.h:136` — `MetaRecord` (48 bytes: 3 node + 3 edge counters).
- `graph_core/struct/pod_struct.h:182,196,209` — `NodeFreeOffset`, `RelationNodeListFreeOffset`, `BatchOfEdgesFreeOffset` (freelist free-offset PODs; replaced `FreeRecord`).
- `graph_core/struct/pod_struct.h:16` — `NodeType` (incl. `COMPLEX = 255`).
- `graph_core/struct/pod_struct.h:41` — `NodeIndex`.
- `graph_core/struct/pod_struct.h:78` — `RelationNodeList` (now 16 bytes: `type_count` + `batch_size`).
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
- `graph_core/odt/node_odt.cpp:27` — `node_to_relation_list` (computes `batch_size`).
- `graph_core/odt/node_odt.cpp:55` — `complex_node_to_record`.
- `graph_core/odt/node_odt.cpp:86` — `reconstruct_neighbors` (stub).
- `graph_core/graph.h:43` — `insert<T>` reuse path (`pop_free_offset` + `write_node_in_freed_slot`).
- `graph_core/graph.h:117` — `delete_node` declaration.
- `graph_core/graph.cpp:163` — `delete_node` (RAM removal + `delete_node_from_disk`).
- `graph_core/io/graph_io.h:65` — `delete_node_from_disk` declaration.
- `graph_core/io/graph_io.h:190` — `write_node_in_freed_slot` template.
- `graph_core/io/graph_io.h:321,333,355` — `freelist_bin_path`, `write_free_offset`, `pop_free_offset`.
- `graph_core/io/graph_io.cpp:380` — `delete_node_from_disk`.
- `graph_core/struct/type_registry.h:51` — `node_record_payload_size` declaration.
- `graph_core/struct/type_registry.cpp` — `node_record_payload_size` definition (out-of-line).
