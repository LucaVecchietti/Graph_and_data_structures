---
id: graph-core-architecture
title: graph_core layered architecture
type: moc
tags: [architecture, layering]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# graph_core layered architecture
> The four layers of graph_core - Domain, POD, ODT, I/O - and which node covers each.

Everything lives under `graph_core/`, split into four layers so the RAM shape and the disk
shape can evolve independently. `Graph` orchestrates; the layers do the work.

## Start here

- [[graph-class]] - the public surface: insert, add_edge, delete_node, traverse.
- [[pod-layout]] - what actually lands on disk.
- [[persistence-io]] - the code that moves bytes between the two.

## The area in detail

1. **Domain** (`struct/domain_struct.h`) - RAM structs: `BaseNode` (type-erased, holds the
   adjacency) plus `Node<T>`, `EdgeRef` and `ComplexRecord`. Covered by [[graph-class]].
2. **POD** (`struct/pod_struct.h`) - packed on-disk records. Covered by [[pod-layout]].
3. **ODT** (`odt/`) - object data transfer, the only layer allowed to convert domain to POD
   and back. Covered by [[odt-layer]].
4. **I/O** (`io/`) - read/write paths, freelists, tombstoning. Covered by [[persistence-io]].

The split is a deliberate choice, not an accident: see [[decision-pod-vs-domain-split]].
Its cost is that most features touch three files at once.

Cross-cutting storage mechanisms: [[freelist]], [[tombstoning]], [[in-edges-index]],
[[complex-nodes]], [[relation-batch]], [[edge-record]], [[type-registry]].

## Open questions

- The on-disk format carries no magic, version or checksum, and is host-byte-order
  dependent, so any POD layout change silently corrupts an existing `db/`.

## Links

- part of [[project-overview]] - the engine area of the root map
- contains [[graph-class]] - the domain layer and orchestrator
- contains [[pod-layout]] - the POD layer
- contains [[odt-layer]] - the translation layer
- contains [[persistence-io]] - the I/O layer
- specified by [[decision-pod-vs-domain-split]] - why the layers are separate at all
