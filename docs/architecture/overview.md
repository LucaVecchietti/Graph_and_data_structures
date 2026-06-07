# Architecture Overview

> Top-level view of the pointer_graphs system: components, layers, and how data moves between them.

| Campo | Valore |
|---|---|
| Tipo | architecture |
| Lingua | en |
| Ultimo aggiornamento | 2026-06-07 |
| Commit di riferimento | 9966603 |
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
                  │  freelist/                       │
                  │   └── {prefix}_{size}.dat (×N)   │
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

- **`main.cpp`** — entry point. Builds a small graph, inserts nodes, adds edges, runs BFS, and (Phases 3-4) deletes a primitive node and a COMPLEX node, re-inserting each to exercise the freelist reuse path (including COMPLEX slot + `prog_number` recycling). Currently a smoke test, not a real CLI.
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
   │ id = new edge ? meta.next_edge_id : existing EdgeRef.id
   │ node->neighborgs[type][end] = EdgeRef{id, weight, ptr}   ← RAM-first
   ▼
update_node_edges      (graph_core/io/graph_io.cpp)  ← persistence since 2026-05-30
   │ read OLD NodeIndex + RelationNodeList        ← identify orphaned regions
   │ log orphaned offsets/sizes to graph_io.log   ← placeholder for the freelist
   │ append NEW RelationNodeList POD              → nodes.dat
   │ for each relation in node->neighborgs:
   │     append edges to edges.dat (fresh chunk)  → captures new edge_offset
   │     append tail entry [name][off][count]     → nodes.dat
   │ open nodes.idx (binary | in | out)           ← NOT app: in-place seek
   │ patch NodeIndex.relation_offset in place     → nodes.idx
   │ if new edge: meta.next_edge_id++; meta.edge_count++; write_meta()
```

Since 2026-05-30, `add_edge` persists. [BUG-001](../legacy/known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco) closed. Trade-off: the old `RelationNodeList` and edge chunks become orphaned bytes — the persistent freelist that will reclaim them is not implemented yet. See [Edge persistence design decision](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch). Since 2026-06-02 each `Edge.id` is globally unique, sourced from `MetaRecord.next_edge_id` and stored in `EdgeRef` ([BUG-002](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi) closed; see [decision](../legacy/design_decisions.md#2026-06-02--id-arco-globale-sorgente-in-metarecordnext_edge_id-memorizzato-in-edgeref)).

### Node delete + freelist reuse flow (since 2026-06-03, completed 2026-06-07)

```
main.cpp
   │ g.delete_node(id)
   ▼
Graph::delete_node       (graph_core/graph.cpp)
   │ lazy-load if id not in RAM (id < meta.next_id) else throw
   │ reverse-index: drop id from in_edges[*] of its out-neighbors; edge_count -= out
   │ inbound cleanup: for each owner in in_edges[id]:
   │     erase id from owner adjacency, update_node_edges(owner), edge_count -= removed
   │ erase from `nodes`, delete the pointer
   ▼
delete_node_from_disk    (graph_core/io/graph_io.cpp)
   │ read NodeIndex (+ ComplexHeader if COMPLEX → real size, remove sidecar, recycle prog)
   │ push NodeRecord region       → db/freelist/{nodes|complex}_<size>.dat
   │ push RelationNodeList region → db/freelist/rel_<size>.dat
   │ push each edge chunk         → db/freelist/edges_<size>.dat
   │ zero the orphaned bytes; tombstone nodes.idx slot (type_id=TOMBSTONE)
   │ meta: node_count--, free_count++, free_edge_count += chunks
   ▼ Graph::delete_node → write_meta(meta)

g.insert(value)          ← reuse path (primitives AND COMPLEX)
   │ pop_free_offset(db/freelist/{nodes|complex}_<size>.dat)
   │ if a freed slot exists:
   │     write_{node|complex}_in_freed_slot  → record + NodeIndex written in place
   │     recycle id; node_count++; free_count-- (meta.next_id NOT bumped)
   │ else: append path (write_node + next_id++)
```

Push (`delete_node`) and pop (`insert`) are O(1); each bin holds one fixed size so a pop is always an exact fit. The inbound-edge cleanup uses the in-RAM reverse index `Graph::in_edges` (rebuilt at load by `build_inbound_index`, O(deg_in) per delete). `update_node_edges`' orphaned regions are still not pushed onto the bins. [BUG-016](../legacy/known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex) and [BUG-014](../legacy/known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex) closed. See the decisions on [tombstone](../legacy/design_decisions.md#2026-06-07--tombstone--azzeramento-delle-regioni-su-delete), [reverse index](../legacy/design_decisions.md#2026-06-07--indice-inverso-degli-archi-entranti-in-ram), [COMPLEX binning](../legacy/design_decisions.md#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato) and [freelist](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo).

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
- [Edge persistence: append + obsolete + in-place index patch](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch) — how `add_edge` persists since 2026-05-30, and the exception that `nodes.idx` is no longer purely append-only.
- [Storage sidecar JSON per nodi COMPLEX](../legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex) — why COMPLEX records put their JSON payload in `attributes/` rather than inline.
- [Freelist a bin segregati per dimensione esatta + cancellazione nodo](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo) — how `delete_node` reclaims space and `insert` reuses freed slots.
- [Tombstone + azzeramento delle regioni su delete](../legacy/design_decisions.md#2026-06-07--tombstone--azzeramento-delle-regioni-su-delete) — how a deleted slot is marked and its bytes zeroed.
- [Indice inverso degli archi entranti in-RAM](../legacy/design_decisions.md#2026-06-07--indice-inverso-degli-archi-entranti-in-ram) — how `delete_node` finds and removes inbound edges in O(deg_in).
- [Bin per-tipo per i record COMPLEX via prog_number zero-paddato](../legacy/design_decisions.md#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato) — how COMPLEX records reuse freed slots.
