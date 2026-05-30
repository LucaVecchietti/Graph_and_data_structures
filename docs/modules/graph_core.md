# Module: graph_core

> The C++17 core of pointer_graphs. Defines the `Graph` class, the dual POD/domain struct hierarchy, the ODT translation layer, and the binary I/O layer.

| Campo | Valore |
|---|---|
| Tipo | module |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-26 |
| Commit di riferimento | 326920c |
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
│   └── type_registry.h         Compile-time C++ type ↔ NodeType mapping
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

### `struct BaseNode` (`struct/domain_struct.h`)
Type-erased base. Holds only the adjacency map:
```cpp
std::unordered_map<std::string,
    std::unordered_map<int, std::pair<int, BaseNode*>>> neighborgs;
```
Outer key: relation type. Inner key: neighbor id. Inner value: `(weight, pointer-to-neighbor)`.

Virtual destructor so deleting through `BaseNode*` frees the derived `Node<T>` correctly.

### `template<class T> struct Node : BaseNode` (`struct/domain_struct.h`)
Adds the typed payload `T data;`. `T` is constrained at use site to be POD (trivially copyable) by `node_to_record`'s `static_assert`.

### `enum class NodeType : uint8_t` (`struct/pod_struct.h`)
On-disk tag for `T`. Values: `INT=0, FLOAT=1, DOUBLE=2, CHAR=3, BOOL=4, COMPLEX=255`. The integer values are stable — changing them breaks the on-disk format. `COMPLEX` is reserved for records carrying a runtime `type_label` + JSON attributes (see `ComplexHeader` and `ComplexRecord` below); its on-disk format follows the [sidecar JSON design](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex) and the write path is present but currently not compilable (see [BUG-009..BUG-014](../legacy/known_bugs.md)). Future primitive types should use values `5..254`.

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
Header for the adjacency list of a node: a single `uint64_t type_count`. Per-relation entries `[uint64_t name_length][bytes...][uint64_t edge_offset][uint64_t edge_count]` are written **immediately after** the POD by `write_relation_node_list`. The variable-width tail is not part of the struct itself.

### `struct Edge` (POD, packed) (`struct/pod_struct.h`)
| Field | Type | Purpose |
|---|---|---|
| `id` | `uint64_t` | Per-node edge index (reset per node, see [BUG-002](../legacy/known_bugs.md)). |
| `weight` | `int64_t` | Edge weight. |
| `to_node` | `uint64_t` | Destination node id. |
| `from_node` | `uint64_t` | Source node id. |

### `struct MetaRecord` (POD, packed) (`struct/pod_struct.h`)
| Field | Type | Purpose |
|---|---|---|
| `next_id` | `uint64_t` | Next id to assign on insert. |
| `node_count` | `uint64_t` | Total nodes ever inserted. |
| `free_count` | `uint64_t` | Reserved for freelist support — currently never written to except as `0`. |

### `struct FreeRecord` (POD, packed) (`struct/pod_struct.h`)
Single `uint64_t offset`. Declared but **not yet used** — placeholder for a future `freelist.dat`.

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
| `prog_number` | `uint64_t` | Monotonic counter used to compose unique sidecar names `{prog_number}_{type_label}.json`. Persisted in `db/attributes/attributes_meta.dat`. |

`read_json_attributes_meta` lazy-creates the file with `prog_number = 0` on first access. The counter is **not yet incremented by the write path** (see [BUG-014](../legacy/known_bugs.md)).

### `struct ComplexRecord` (`struct/domain_struct.h`)
RAM-side representation of a `COMPLEX` node payload. Not POD — contains `std::string`.
| Field | Type | Purpose |
|---|---|---|
| `type_label` | `std::string` | Runtime-typed label (e.g. `"Athlete"`, `"Item"`, `"Company"`). |
| `json_attributes` | `std::string` | JSON-encoded attributes of the record (lives in RAM; on disk it is written to a sidecar file, not inline). |

Because `ComplexRecord` is not trivially copyable, it **cannot** flow through the existing `node_to_record` / `write_pod` path: that template would fail the `static_assert` in `odt/node_odt.h:22`. The COMPLEX branch of `write_node` routes through `complex_node_to_record` (computes the sidecar path) + `write_complex` (writes the header, the two length-prefixed strings, and the sidecar JSON file). Since 2026-05-30, both this branch and `Graph::insert<ComplexRecord>` compile ([BUG-010](../legacy/known_bugs.md) and [BUG-015](../legacy/known_bugs.md) fixed); the only remaining COMPLEX-related open item is [BUG-014](../legacy/known_bugs.md) (`prog_number` never persisted).

### `template<class T> struct node_type_of` (`struct/type_registry.h`)
Compile-time `T → NodeType` map. Primary template is intentionally undefined; specializations exist for `int, float, double, char, bool` and `ComplexRecord` (→ `COMPLEX`). Using an unsupported `T` triggers a clear compile error. Convenience: `node_type_of_v<T>`. The `ComplexRecord → COMPLEX` mapping is defined and `write_node` / `read_typed_node` / `Graph::insert` all dispatch on it via `if constexpr`. The full insert→write→read path compiles since 2026-05-30 ([BUG-010](../legacy/known_bugs.md) and [BUG-015](../legacy/known_bugs.md) fixed).

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
- `MetaRecord meta` — in-RAM copy of `meta.dat`.
- `Logger logger` — writes to `graph.log` with `DEBUG` floor.

**Private methods:**
- `void init_meta()` — zero-fill `meta` and write `meta.dat`. Called by the constructor when no `meta.dat` exists.
- `void load_meta()` — read `meta.dat` into `meta`.

**Public methods:** see next section.

## Funzioni / interfacce esposte

### `Graph::Graph()` (`graph.cpp:8`)
Creates `DB_PATH` if missing. If `meta.dat` doesn't exist or is zero-sized, calls `init_meta()`; otherwise `load_meta()`.

### `Graph::~Graph()` (`graph.cpp:21`)
`delete`s every node in `nodes`. The map itself is destroyed by the `unordered_map` destructor.

### `template<class T> void Graph::insert(T&& value)` (`graph.h:43`)
Allocates a `Node<ValueType>` (with `T` stripped of reference), forwards `value` into `data`, places the node in `nodes[meta.next_id]`, calls `write_node(*newNode, meta)`, increments `meta.node_count` and `meta.next_id`, logs at INFO, then `write_meta(meta)`.

- Throws: whatever `write_node` / `write_meta` throw (runtime_error on file open failure).
- Side effects: writes to `db/nodes.dat`, `db/nodes.idx`, `db/edges.dat` (empty edges section), `db/meta.dat`; appends to `graph.log` and stderr.

### `void Graph::add_edge(int start, int end, std::string type = "", int weight = 1)` (`graph.cpp:53`)
1. Rejects `type` longer than `RELATION_TYPE_MAX_SIZE` (throws `std::invalid_argument`).
2. For each endpoint not in RAM: if id `< meta.next_id`, `read_node` from disk and cache; else throw `std::out_of_range`.
3. Inserts `(weight, end_ptr)` into `start->neighborgs[type][end]`.

**Side effects on disk: none.** The mutation is RAM-only — see [BUG-001](../legacy/known_bugs.md).

### `template<class Policy, class NodeFn, class EdgeFn> void Graph::traverse(int start, const std::string& type, NodeFn on_node, EdgeFn on_edge)` (`graph.h:76`)
Generic graph traversal. Maintains `visited` and a `Policy::Frontier`. On visit, calls `on_node(idx)` and pushes to the frontier. On each pop, iterates the node's neighbors for the requested `type`, calls `on_edge(from, to, weight)` for every edge, and visits unvisited targets. Missing node or missing relation → silently skipped.

### `bfs(...)` / `dfs(...)` (`graph.h:115` / `graph.h:121`)
Thin wrappers selecting `BFSPolicy` / `DFSPolicy`.

### I/O helpers (`io/graph_io.h`)

| Function | Purpose |
|---|---|
| `void write_node_index(uint64_t record_offset, uint64_t relation_offset, NodeType, std::ofstream&, const MetaRecord&)` | Builds and writes a `NodeIndex` to `nodes.idx`. `idx.id = meta.next_id`. |
| `void write_complex(const ComplexRecord&, std::ofstream&)` | Writes the COMPLEX payload to `nodes.dat` (header + two length-prefixed strings) and opens the sidecar JSON file under `JSON_ATTR_PATH`. Still not compilable as-is: calls `write_pod` on a non-POD `ComplexRecord` and uses a sidecar path inconsistent with the one stored in the header — see [BUG-011](../legacy/known_bugs.md), [BUG-013](../legacy/known_bugs.md). (Previously also had a duplicate stub, see [BUG-009](../legacy/known_bugs.md) — fixed 2026-05-30.) |
| `void read_complex(ComplexRecord&, std::ifstream&)` | Reads the COMPLEX payload (header + two length-prefixed strings) from `nodes.dat` and slurps the sidecar JSON file under `JSON_ATTR_PATH` into `out.json_attributes`. Throws `runtime_error` if the sidecar is missing. Called by the `if constexpr` COMPLEX branch of `read_typed_node<T>`. |
| `NodeIndex read_node_index(std::ifstream&)` | Reads one `NodeIndex` from the current stream position. |
| `std::vector<RelationEntry> read_relation_node_list(std::ifstream&)` | Reads the `RelationNodeList` header and its variable-width tail. |
| `void write_meta(const MetaRecord&)` | Truncates and rewrites `meta.dat`. |
| `MetaRecord read_meta()` | Reads `meta.dat`. |
| `void write_json_attributes_meta(const JsonMeta&)` | Truncates and rewrites `db/attributes/attributes_meta.dat`. |
| `JsonMeta read_json_attributes_meta()` | Reads the `JsonMeta` POD; lazy-creates the file with `prog_number = 0` if missing. Throws on empty/unreadable file. |
| `template<T> uint64_t write_node_record(const Node<T>&)` | Appends a `NodeRecord<T>` to `nodes.dat`. Returns the offset. |
| `template<T> uint64_t write_relation_node_list(const Node<T>&, uint64_t node_id, std::ofstream& out)` | Appends `RelationNodeList` header + tail to `out` (already-open `nodes.dat`), and appends each edge to `edges.dat`. |
| `template<T> void write_node(const Node<T>&, const MetaRecord&)` | Composes the three writes for a full node persist (record + relations + index). Dispatches the record write via `if constexpr (node_type_of_v<T> == NodeType::COMPLEX)`: primitives go through `node_to_record` + `write_pod`, `COMPLEX` goes through `complex_node_to_record` + `write_complex`. Compiles since 2026-05-30 ([BUG-010](../legacy/known_bugs.md) fixed). |
| `BaseNode* read_node(uint64_t id)` | Reads `NodeIndex` at `id * sizeof(NodeIndex)` in `nodes.idx`, dispatches on `type_id` to the right `read_typed_node<T>`. No `case NodeType::COMPLEX:` yet — reading a COMPLEX index throws `"Unknown NodeType"`. |
| `template<T> NodeRecord<T> read_node_record(std::ifstream&)` | Reads one `NodeRecord<T>`. |
| `template<T> BaseNode* read_typed_node(const NodeIndex&, std::ifstream& dat_in)` | Builds a fresh `Node<T>` on the heap with data + neighbors (neighbor pointers left `nullptr`). |

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
| `NodeRecord<ComplexHeader> complex_node_to_record(const Node<ComplexRecord>&, std::string &json_file_path)` (`node_odt.cpp:55`) | COMPLEX-specific ODT bridge: reads `JsonMeta`, composes the sidecar path into the out-param `json_file_path` as `{prog_number}_{type_label}.json` (uses `std::to_string`, see [BUG-011](../legacy/known_bugs.md) fix), builds a `ComplexHeader` from `type_label.size()` and `json_file_path.size()`, and returns it wrapped in `NodeRecord<ComplexHeader>`. The returned `NodeRecord` is currently unused by `write_node` (the header is rebuilt inside `write_complex` from the same inputs). Still does not persist the incremented `prog_number` — see [BUG-014](../legacy/known_bugs.md). |
| `RelationNodeList node_to_relation_list(const BaseNode&)` (`node_odt.cpp:27`) | Fills only `type_count`. The variable-width tail is written separately by `write_relation_node_list`. |
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
- C++ standard library only (`<unordered_map>`, `<unordered_set>`, `<vector>`, `<string>`, `<stdexcept>`, `<filesystem>`, `<fstream>`, `<queue>`, `<stack>`, `<type_traits>`, `<ctime>`).

No third-party libraries. No dependency on `data_tructures/`.

## Voci legacy collegate

- [Storage sidecar JSON per nodi COMPLEX](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex)
- [Introduzione tag NodeType::COMPLEX + ComplexRecord (WIP)](../legacy/design_decisions.md#2026-05-26--introduzione-tag-nodetypecomplex--complexrecord-wip)
- [Separazione POD vs Domain struct](../legacy/design_decisions.md#2026-05-26--separazione-pod-vs-domain-struct)
- [Type-erased BaseNode + Node&lt;T&gt;](../legacy/design_decisions.md#2026-05-26--type-erased-basenode--nodet)
- [Policy-based traversal](../legacy/design_decisions.md#2026-05-26--policy-based-traversal-bfsdfs)
- [Append-only data files, truncated meta](../legacy/design_decisions.md#2026-05-26--append-only-data-files-truncated-meta)
- [Single-open append su nodes.dat](../legacy/design_decisions.md#2026-05-26--single-open-append-su-nodesdat)
- [API — ComplexHeader rinominato](../legacy/api_changes.md#2026-05-26--complexheaderjson_attributes_size--json_file_path_size)
- [API — write_node switch su NodeType](../legacy/api_changes.md#2026-05-26--write_nodet-switch-su-nodetype-per-ramo-complex)
- [BUG-001 — add_edge non persiste](../legacy/known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco)
- [BUG-002 — edge_idx non globale](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi)
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
- `graph_core/graph.cpp:53` — `add_edge`.
- `graph_core/struct/domain_struct.h:15` — `BaseNode`.
- `graph_core/struct/domain_struct.h:38` — `ComplexRecord`.
- `graph_core/struct/pod_struct.h:16` — `NodeType` (incl. `COMPLEX = 255`).
- `graph_core/struct/pod_struct.h:41` — `NodeIndex`.
- `graph_core/struct/pod_struct.h:134` — `ComplexHeader` (field `json_file_path_size`).
- `graph_core/struct/pod_struct.h:148` — `JsonMeta`.
- `graph_core/struct/functions_policies.h:12` — `BFSPolicy`.
- `graph_core/struct/type_registry.h:33` — `node_type_of<ComplexRecord> → COMPLEX`.
- `graph_core/costants.h:7-11` — `DB_PATH`, `META_FILE_PATH`, `JSON_ATTR_META_PATH`, `JSON_ATTR_PATH`.
- `graph_core/io/graph_io.h:24` — `write_complex` declaration.
- `graph_core/io/graph_io.h:38` — `read_complex` declaration.
- `graph_core/io/graph_io.h:107` — `write_node` template (switch on `NodeType`).
- `graph_core/io/graph_io.cpp:34` — `write_complex` (single definition since 2026-05-30, see [BUG-009](../legacy/known_bugs.md)).
- `graph_core/io/graph_io.cpp:65` — `read_complex`.
- `graph_core/io/graph_io.cpp:120` — `read_node` (type dispatch — COMPLEX case routes to `read_typed_node<ComplexRecord>`).
- `graph_core/io/graph_io.cpp:172,190` — `write_json_attributes_meta`, `read_json_attributes_meta`.
- `graph_core/odt/node_odt.cpp:55` — `complex_node_to_record`.
- `graph_core/odt/node_odt.cpp:86` — `reconstruct_neighbors` (stub).
