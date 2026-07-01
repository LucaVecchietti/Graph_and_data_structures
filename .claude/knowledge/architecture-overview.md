---
node: architecture-overview
title: Three-Layer Architecture (Domain / POD / ODT)
type: domain
tags: [architecture, layering]
updated: 2026-07-01
---

# Three-Layer Architecture

The project deliberately keeps RAM shape and disk shape apart so they can evolve independently. Three layers live inside `graph_core/`.

## Facts

- **Domain** (`struct/domain_struct.h`) — `BaseNode` (type-erased, holds the adjacency map) + `template<class T> Node : BaseNode` (typed payload). Adjacency is `unordered_map<string relation, unordered_map<int neighbor_id, pair<int weight, BaseNode*>>>`.
- **POD** (`struct/pod_struct.h`) — packed (`#pragma pack(push,1)`) on-disk records: `NodeIndex`, `NodeRecord<T>`, `NodeRelationList`, `Edge` (with `prev_offset`/`next_offset`), `MetaRecord`, WIP `ComplexHeader`. No magic/version/checksum; host-byte-order-dependent, ABI-fragile.
- **ODT** (`odt/`) — Object Data Transfer: the *only* layer allowed to convert between Domain and POD. Functions: `node_to_record`, `node_to_relation_list`, `reconstruct_neighbors`, `edge_to_pod`.
- `T` maps to its on-disk `NodeType` tag via compile-time `node_type_of<T>` in `struct/type_registry.h`. Valid payloads: `int, float, double, char, bool` (and WIP `ComplexRecord`).

## Relations

- **contains** → [[graph-core]]
- **contains** → [[persistence-io]]
- **relates-to** → [[glossary-load-bearing-typos]]
- **part-of** → [[Index]]
- **documented-in** → docs/architecture/overview.md

## Sources

- `graph_core/struct/domain_struct.h`, `graph_core/struct/pod_struct.h`
- `graph_core/odt/`, `graph_core/struct/type_registry.h`
