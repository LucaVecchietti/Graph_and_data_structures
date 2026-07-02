---
node: Index
title: pointer_graphs — Knowledge Graph Root
type: root
tags: [root, index]
updated: 2026-07-01
---

# pointer_graphs — Knowledge Graph

**Purpose.** Claude's internal map of `pointer_graphs`: a C++17 persistent, typed, weighted graph store with an append-only binary on-disk format, size-segregated freelists, tombstoning, and O(1) edge operations. Reload the mental model by starting here and walking its wikilinks.

**How to read it.** Start at this root → enter a cluster → follow typed relations (`part-of`, `depends-on`, `contains`, …). Every node is reachable from here.

> ⚠️ Read [[doc-drift-caveats]] first: `CLAUDE.md` and `docs/legacy/known_bugs.md` describe an OLDER state than the code (BUG-001 fixed, BUG-003/004 removed). This graph reflects the **current code**, verified against source.

## Clusters

- [[architecture-overview]] — the layered design (Domain / POD / ODT / I/O) and how data flows. Contains the four code-layer clusters below.
  - [[graph-core]] — `Graph` class + in-RAM domain model + traversal.
  - [[pod-layout]] — packed on-disk POD records (`pod_struct.h`).
  - [[odt-layer]] — Domain↔POD translation (`odt/`).
  - [[persistence-io]] — disk read/write paths, freelists, tombstoning (`io/`).
- [[build-and-run]] — build, run, the working-directory trap, and the smoke test.

## Node registry

- [[architecture-overview]] — four layers; format is packed, host-byte-order, no magic/version/checksum.
- [[graph-core]] — owns `unordered_map<int,BaseNode*>`, `in_edges`, `meta`; insert/add_edge/delete_node/traverse.
- [[pod-layout]] — NodeIndex (25B, O(1) lookup), NodeRecord, MetaRecord, freelist PODs, NodeType tags.
- [[odt-layer]] — node_to_record / complex_node_to_record / node_to_relation_list / edge_to_pod; RelationEntry.
- [[persistence-io]] — write_node / read_node / persist_new_edge / persist_edge_weight / update_node_edges / build_inbound_index.
- [[traversal-policies]] — BFSPolicy(queue)/DFSPolicy(stack) + single `traverse<Policy>` template.
- [[in-edges-index]] — RAM-only reverse edge index for O(deg_in) delete.
- [[freelist]] — size-segregated LIFO bins under `db/freelist/`, O(1) exact-fit reuse.
- [[tombstoning]] — NodeType::TOMBSTONE=254; idx slot survives for id recycling.
- [[logger]] — file + stderr logger (`graph.log`, `graph_io.log`).
- [[relation-batch]] — fixed-width 2213B relation batch; O(1) in-place line rewrite.
- [[edge-record]] — Edge POD; doubly-linked (node,relation) chain; O(1) splice.
- [[complex-nodes]] — NodeType::COMPLEX=255; type_label + JSON sidecar under `db/attributes/`.
- [[type-registry]] — compile-time `node_type_of<T>` → NodeType; `node_record_payload_size`.
- [[smoke-test]] — `main.cpp`, 6-phase hand-driven regression test.
- [[glossary-load-bearing-typos]] — misspellings that are load-bearing (`neighborgs`, `data_tructures`, `costants`).
- [[legacy-c-prototypes]] — out-of-build C prototypes (`data_tructures/`, `node_n_pointers.c`).
- [[doc-drift-caveats]] — where CLAUDE.md / known_bugs.md have drifted from the code.

## Maintenance

Maintained by the `knowledge-graph` skill. New knowledge → add/update a node, write the inverse relation on its target, register it here.
