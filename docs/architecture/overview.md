# Architecture Overview

> Top-level view of the pointer_graphs system: components, layers, and how data moves between them.

| Campo | Valore |
|---|---|
| Tipo | architecture |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-26 |
| Commit di riferimento | 326920c |
| Mirror | — |

---

## Overview

`pointer_graphs` is an experimental in-memory + on-disk graph store written primarily in C++17. Nodes carry typed payloads (`int`, `float`, `double`, `char`, `bool`), edges carry a relation type (string) and a weight. The system separates a **RAM-optimized domain representation** from a **disk-optimized POD representation**, with an explicit translation layer (ODT — Object Data Transfer) bridging the two.

A standalone C subtree (`data_tructures/`, `node_n_pointers.c`) holds the original prototype written in plain C; it is kept for reference and is not linked into the current build.

## Diagramma

```
                            ┌────────────┐
                            │  main.cpp  │
                            └─────┬──────┘
                                  │ uses
                                  ▼
                  ┌───────────────────────────────┐
                  │     graph_core (C++17)        │
                  │                               │
                  │   ┌────────┐                  │
                  │   │ Graph  │   in-memory      │
                  │   └───┬────┘   adjacency      │
                  │       │                       │
                  │   ┌───▼────┐                  │
                  │   │ struct │  domain + POD    │
                  │   │ /odt   │  + translation   │
                  │   └───┬────┘                  │
                  │       │                       │
                  │   ┌───▼────┐                  │
                  │   │   io   │  binary read/    │
                  │   │        │  write           │
                  │   └───┬────┘                  │
                  └───────┼───────────────────────┘
                          │
                          ▼
                  ┌──────────────────────────────────┐
                  │   db/ (disk)                     │
                  │  nodes.dat                       │
                  │  nodes.idx                       │
                  │  edges.dat                       │
                  │  meta.dat                        │
                  │  attributes/                     │
                  │   ├── attributes_meta.dat        │
                  │   └── {prog}_{label}.json (×N)   │
                  └──────────────────────────────────┘

       ┌────────────────────────────┐
       │  data_tructures/  (C)      │  not linked — legacy prototype
       │  map_hash_table.{c,h}      │
       └────────────────────────────┘

       ┌────────────────────────────┐
       │  node_n_pointers.c (C)     │  not linked — earlier design
       │  adjacency-matrix variant  │
       └────────────────────────────┘
```

## Componenti

- **`main.cpp`** — entry point. Builds a small graph, inserts nodes, adds edges, runs BFS. Currently a smoke test, not a real CLI.
- **`graph_core/`** — see [modules/graph_core.md](../modules/graph_core.md).
  - `graph.h/cpp` — `Graph` class: in-memory adjacency, BFS/DFS, persistence hooks.
  - `struct/` — POD and domain structs, traversal policies, type registry.
  - `odt/` — domain ↔ POD translation.
  - `io/` — binary serialization to/from `db/`.
  - `logger.h`, `costants.h` — shared utilities/constants.
- **`db/`** — on-disk persistence. See [modules/db.md](../modules/db.md).
- **`data_tructures/`** — generic hash table in C. See [modules/data_structures.md](../modules/data_structures.md). Not linked by the current CMake target.
- **`node_n_pointers.c`** — first-iteration prototype in C using an adjacency matrix and raw node arrays. Kept for historical context.

## Flussi di dati

### Insert flow

```
main.cpp
   │ g.insert(value)
   ▼
Graph::insert<T>      (graph_core/graph.h)
   │ new Node<T>; node->data = value
   │ nodes[meta.next_id] = node             ← RAM domain struct
   │ write_node(*node, meta)
   ▼
write_node<T>          (graph_core/io/graph_io.h)
   │ if constexpr (node_type_of_v<T> == NodeType::COMPLEX)
   │ ├── primitives (INT/FLOAT/DOUBLE/CHAR/BOOL):  (else branch)
   │ │     NodeRecord<T>                   → nodes.dat
   │ └── COMPLEX:                          (compiles since 2026-05-30 — BUG-010/011/013 fixed)
   │       read_json_attributes_meta()     ← attributes/attributes_meta.dat
   │       complex_node_to_record(node, json_file_path)
   │       write_complex(node.data, json_file_path, dat_out)
   │                                       → ComplexHeader + 2 strings → nodes.dat
   │                                       → JSON payload              → attributes/{prog}_{label}.json
   │ RelationNodeList header              → nodes.dat
   │ NodeIndex                            → nodes.idx
   │ (per-relation edges)                 → edges.dat
   ▼
write_meta(meta)       → meta.dat
```

### Edge addition flow

```
main.cpp
   │ g.add_edge(start, end, type, weight)
   ▼
Graph::add_edge        (graph_core/graph.cpp)
   │ if start/end not in RAM but id < meta.next_id:
   │     read_node(id)   ← lazy load from disk
   │ node->neighborgs[type][end] = (weight, ptr)
```

Note: `add_edge` currently mutates RAM only; the edge is NOT re-flushed to `edges.dat` after the initial `write_node`. See [known_bugs.md](../legacy/known_bugs.md).

### Read-back flow

```
read_node(id)           (graph_core/io/graph_io.cpp)
   │ seek nodes.idx at id * sizeof(NodeIndex)
   │ read NodeIndex
   │ switch on type_id → read_typed_node<T>
   ▼
read_typed_node<T>      (graph_core/io/graph_io.h)
   │ seek nodes.dat at NodeIndex.offset
   │ read NodeRecord<T>
   │ seek nodes.dat at NodeIndex.relation_offset
   │ read RelationNodeList header + entries
   │ for each entry: read edges from edges.dat
   │ neighbor pointers left as nullptr  ← must be re-linked later
```

## Dipendenze cross-modulo

```
main.cpp
   └─ graph_core/graph.h
        ├─ graph_core/struct/{domain,pod,functions_policies,type_registry}.h
        ├─ graph_core/odt/{node_odt,edge_odt}.h
        │     └─ graph_core/struct/{domain,pod}.h
        ├─ graph_core/io/graph_io.h
        │     ├─ graph_core/struct/{pod,domain,type_registry}.h
        │     ├─ graph_core/odt/{node_odt,edge_odt}.h
        │     └─ graph_core/io/io_utils.h
        ├─ graph_core/costants.h
        └─ graph_core/logger.h
```

`data_tructures/` and `node_n_pointers.c` have no inbound dependencies from the C++ side — they are isolated.

## Voci legacy collegate

- [POD vs Domain split](../legacy/design_decisions.md#2026-05-26--separazione-pod-vs-domain-struct) — why two parallel struct hierarchies exist.
- [Policy-based traversal](../legacy/design_decisions.md#2026-05-26--policy-based-traversal-bfsdfs) — why BFS/DFS share one `traverse` template.
- [Type-erased BaseNode + Node&lt;T&gt;](../legacy/design_decisions.md#2026-05-26--type-erased-basenode--nodet) — why nodes of different payload types can coexist.
- [Append-only data files, truncated meta](../legacy/design_decisions.md#2026-05-26--append-only-data-files-truncated-meta) — file open modes.
- [Storage sidecar JSON per nodi COMPLEX](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex) — why COMPLEX records put their JSON payload in `attributes/` rather than inline.
