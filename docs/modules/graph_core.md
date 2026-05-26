# Module: graph_core

> The C++17 core of pointer_graphs. Defines the `Graph` class, the dual POD/domain struct hierarchy, the ODT translation layer, and the binary I/O layer.

| Campo | Valore |
|---|---|
| Tipo | module |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-26 |
| Commit di riferimento | b6e7304-dirty |
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
On-disk tag for `T`. Values: `INT=0, FLOAT=1, DOUBLE=2, CHAR=3, BOOL=4, COMPLEX=255`. The integer values are stable — changing them breaks the on-disk format. `COMPLEX` is reserved for records carrying a runtime `type_label` + JSON attributes (see `ComplexHeader` and `ComplexRecord` below); its on-disk format is WIP. Future primitive types should use values `5..254`.

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
| `json_attributes_size` | `uint64_t` | Byte length of the JSON-attributes string that follows. |

The two strings (`type_label` then `json_attributes`) are written **after** the header as raw bytes (no NUL terminator), in this order. The on-disk serialization path for COMPLEX is not implemented yet — see [the legacy entry](../legacy/design_decisions.md#2026-05-26--introduzione-tag-nodetypecomplex--complexrecord-wip).

### `struct ComplexRecord` (`struct/domain_struct.h`)
RAM-side representation of a `COMPLEX` node payload. Not POD — contains `std::string`.
| Field | Type | Purpose |
|---|---|---|
| `type_label` | `std::string` | Runtime-typed label (e.g. `"Athlete"`, `"Item"`, `"Company"`). |
| `json_attributes` | `std::string` | JSON-encoded attributes of the record. |

Because `ComplexRecord` is not trivially copyable, it **cannot** flow through the existing `write_pod` / `NodeRecord<T>` path: `Graph::insert<ComplexRecord>(...)` currently fails the `static_assert` in `node_to_record` (`odt/node_odt.h:22`). A dedicated write path is required (see WIP note in the [design decision](../legacy/design_decisions.md#2026-05-26--introduzione-tag-nodetypecomplex--complexrecord-wip)).

### `template<class T> struct node_type_of` (`struct/type_registry.h`)
Compile-time `T → NodeType` map. Primary template is intentionally undefined; specializations exist for `int, float, double, char, bool` and `ComplexRecord` (→ `COMPLEX`). Using an unsupported `T` triggers a clear compile error. Convenience: `node_type_of_v<T>`. Note: the `ComplexRecord → COMPLEX` mapping is defined, but the corresponding I/O path is WIP — having the mapping does **not** mean `Graph::insert<ComplexRecord>` works yet.

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
| `NodeIndex read_node_index(std::ifstream&)` | Reads one `NodeIndex` from the current stream position. |
| `std::vector<RelationEntry> read_relation_node_list(std::ifstream&)` | Reads the `RelationNodeList` header and its variable-width tail. |
| `void write_meta(const MetaRecord&)` | Truncates and rewrites `meta.dat`. |
| `MetaRecord read_meta()` | Reads `meta.dat`. |
| `template<T> uint64_t write_node_record(const Node<T>&)` | Appends a `NodeRecord<T>` to `nodes.dat`. Returns the offset. |
| `template<T> uint64_t write_relation_node_list(const Node<T>&, uint64_t node_id, std::ofstream& out)` | Appends `RelationNodeList` header + tail to `out` (already-open `nodes.dat`), and appends each edge to `edges.dat`. |
| `template<T> void write_node(const Node<T>&, const MetaRecord&)` | Composes the three writes for a full node persist (record + relations + index). |
| `BaseNode* read_node(uint64_t id)` | Reads `NodeIndex` at `id * sizeof(NodeIndex)` in `nodes.idx`, dispatches on `type_id` to the right `read_typed_node<T>`. |
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
| `RelationNodeList node_to_relation_list(const BaseNode&)` (`node_odt.cpp:22`) | Fills only `type_count`. The variable-width tail is written separately by `write_relation_node_list`. |
| `NodeIndex node_to_node_index(uint64_t id, uint64_t record_offset, uint64_t relation_offset)` (`node_odt.cpp:34`) | Builder for `NodeIndex` (currently unused — `write_node_index` builds the struct inline). |
| `std::unordered_map<...> reconstruct_neighbors(const RelationNodeList&)` (`node_odt.cpp:46`) | **Stub — returns an empty map.** See [BUG-003](../legacy/known_bugs.md). |
| `template<T> BaseNode node_form_pod(const NodeIndex&, const NodeRecord<T>&, const RelationNodeList&)` (`node_odt.h:54`) | **Currently broken** — references `node.neihborgs` (typo, missing `b`). See [BUG-004](../legacy/known_bugs.md). |
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

- [Introduzione tag NodeType::COMPLEX + ComplexRecord (WIP)](../legacy/design_decisions.md#2026-05-26--introduzione-tag-nodetypecomplex--complexrecord-wip)
- [Separazione POD vs Domain struct](../legacy/design_decisions.md#2026-05-26--separazione-pod-vs-domain-struct)
- [Type-erased BaseNode + Node&lt;T&gt;](../legacy/design_decisions.md#2026-05-26--type-erased-basenode--nodet)
- [Policy-based traversal](../legacy/design_decisions.md#2026-05-26--policy-based-traversal-bfsdfs)
- [Append-only data files, truncated meta](../legacy/design_decisions.md#2026-05-26--append-only-data-files-truncated-meta)
- [Single-open append su nodes.dat](../legacy/design_decisions.md#2026-05-26--single-open-append-su-nodesdat)
- [BUG-001 — add_edge non persiste](../legacy/known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco)
- [BUG-002 — edge_idx non globale](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi)
- [BUG-003 — reconstruct_neighbors stub](../legacy/known_bugs.md#2026-05-26--bug-003-reconstruct_neighbors-non-implementata)
- [BUG-004 — typo neihborgs](../legacy/known_bugs.md#2026-05-26--bug-004-typo-neihborgs-in-node_form_pod)
- [BUG-005 — logger globale duplicato](../legacy/known_bugs.md#2026-05-26--bug-005-logger-globale-duplicato-in-graph_iocpp)

## Riferimenti

- `graph_core/graph.h:19` — `class Graph`.
- `graph_core/graph.h:43` — `insert<T>` template.
- `graph_core/graph.h:76` — `traverse<Policy, ...>` template.
- `graph_core/graph.cpp:53` — `add_edge`.
- `graph_core/struct/domain_struct.h:15` — `BaseNode`.
- `graph_core/struct/domain_struct.h:33` — `ComplexRecord`.
- `graph_core/struct/pod_struct.h:16` — `NodeType` (incl. `COMPLEX = 255`).
- `graph_core/struct/pod_struct.h:41` — `NodeIndex`.
- `graph_core/struct/pod_struct.h:134` — `ComplexHeader`.
- `graph_core/struct/functions_policies.h:12` — `BFSPolicy`.
- `graph_core/struct/type_registry.h:20` — `node_type_of` (incl. `ComplexRecord → COMPLEX`).
- `graph_core/io/graph_io.cpp:49` — `read_node` (type dispatch — `COMPLEX` case TBD).
- `graph_core/odt/node_odt.cpp:46` — `reconstruct_neighbors` (stub).
