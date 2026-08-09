# Architecture Overview

> Top-level view of the pointer_graphs system: components, layers, and how data moves between them.

| Campo | Valore |
|---|---|
| Tipo | architecture |
| Lingua | en |
| Ultimo aggiornamento | 2026-08-09 |
| Commit di riferimento | fbc6703 |
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
   │ NodeRelationList chain (2213 B each)  → nodes.dat
   │ NodeIndex                            → nodes.idx
   │ (per-relation edge chains)           → edges.dat
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
   │ resolve: new edge (fresh id) vs overwrite (existing EdgeRef.id + .offset)
   ▼
NEW edge → persist_new_edge   (graph_core/io/graph_io.cpp)   ← O(1)
   │ read relation_offset from nodes.idx; walk the batch chain to the line (or the last batch)
   │ alloc Edge slot: pop edges_48 bin, else append edges.dat
   │ write Edge {prev=0, next=old_head}; patch old head's prev_offset in place
   │ update ONE relation line in place (new head + count) — or add a line + header
   │ to the LAST batch — or, if that one is full, allocate a batch and link it
   │ by patching its next_offset (chaining, since 2026-08-09)
   │ (no batch ever moves → nodes.idx untouched); store returned offset in EdgeRef
   │ in_edges[end].insert(start); meta.next_edge_id++; meta.edge_count++
OVERWRITE → persist_edge_weight(EdgeRef.offset, weight)        ← O(1), 8 bytes in place
   ▼
write_meta(meta)
```

`add_edge` is **O(1)** since 2026-06-19: a new edge appends + splices at the chain head and rewrites one fixed-width relation line in place; an overwrite writes 8 bytes in place. The relation batch never moves, so `nodes.idx` is untouched and `nodes.dat` does not grow. `add_edge` no longer calls `update_node_edges` (which survives only for `delete_node`'s inbound cleanup). [BUG-001](../legacy/known_bugs.md#2026-05-26--bug-001-add_edge-non-persiste-su-disco) closed; ids stable since 2026-06-02 ([BUG-002](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi)). See the [O(1) add_edge decision](../legacy/design_decisions.md#2026-06-19--add_edge-in-o1-append--relink--in-place-line-update) and the [fixed-width format decision](../legacy/design_decisions.md#2026-06-19--relation-list-a-batch-fixed-width--edge-a-lista-doppiamente-concatenata). See [Edge persistence design decision](../legacy/design_decisions.md#2026-05-30--edge-persistence-append--obsolete--in-place-index-patch). Since 2026-06-02 each `Edge.id` is globally unique, sourced from `MetaRecord.next_edge_id` and stored in `EdgeRef` ([BUG-002](../legacy/known_bugs.md#2026-05-26--bug-002-edgeid-non-globale-tra-nodi) closed; see [decision](../legacy/design_decisions.md#2026-06-02--id-arco-globale-sorgente-in-metarecordnext_edge_id-memorizzato-in-edgeref)).

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
   │ push EVERY NodeRelationList batch of the chain → db/freelist/rel_2213.dat
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

Push (`delete_node`) and pop (`insert`) are O(1); each bin holds one fixed size so a pop is always an exact fit. The inbound-edge cleanup uses the in-RAM reverse index `Graph::in_edges` (rebuilt at load by `build_inbound_index`, O(deg_in) per delete). `update_node_edges`' orphaned regions are pushed onto the `rel`/`edges` bins since [BUG-017](../legacy/known_bugs.md#2026-06-07--bug-017-update_node_edges-orfanizza-regioni-senza-spingerle-sulla-freelist), and since 2026-06-13 those bins are also **reused** on edge rewrite (pop-then-append, exact fit) — see [Reuse of the rel/edges freelist bins](../legacy/design_decisions.md#2026-06-13--reuse-of-the-reledges-freelist-bins-edge-space-compaction). [BUG-016](../legacy/known_bugs.md#2026-06-03--bug-016-delete_node-prototipo-non-aggiorna-idx-contatori-meta-archi-entranti-complex) and [BUG-014](../legacy/known_bugs.md#2026-05-26--bug-014-prog_number-mai-incrementatopersistito-dopo-write-complex) closed. See the decisions on [tombstone](../legacy/design_decisions.md#2026-06-07--tombstone--azzeramento-delle-regioni-su-delete), [reverse index](../legacy/design_decisions.md#2026-06-07--indice-inverso-degli-archi-entranti-in-ram), [COMPLEX binning](../legacy/design_decisions.md#2026-06-07--bin-per-tipo-per-i-record-complex-via-prog_number-zero-paddato) and [freelist](../legacy/design_decisions.md#2026-06-03--freelist-a-bin-segregati-per-dimensione-esatta--cancellazione-nodo).

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
   │ read the batch CHAIN: header + fixed-width lines, hop next_offset until 0
   │ for each line: walk the edge chain from edge_offset via next_offset
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
- [Relation-list a batch fixed-width + Edge a lista doppiamente concatenata](../legacy/design_decisions.md#2026-06-19--relation-list-a-batch-fixed-width--edge-a-lista-doppiamente-concatenata) — the fixed-width relation batch and edge linked-list format (groundwork for O(1) `add_edge`).
- [add_edge in O(1): append + relink + in-place line update](../legacy/design_decisions.md#2026-06-19--add_edge-in-o1-append--relink--in-place-line-update) — how `add_edge`/overwrite became O(1) on that format.
- [Relation-batch chaining: catena di batch via next_offset](../legacy/design_decisions.md#2026-08-09--relation-batch-chaining-catena-di-batch-via-next_offset) — how a node goes past 8 relation types without moving anything.
