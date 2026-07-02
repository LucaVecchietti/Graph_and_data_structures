---
node: architecture-overview
title: Layered Architecture (Domain / POD / ODT / I/O)
type: domain
tags: [architecture, layering]
updated: 2026-07-01
---

# Layered Architecture

Everything lives under `graph_core/`, split so the RAM shape and the disk shape can evolve independently. `Graph` orchestrates; four layers do the work.

## Facts

- **Domain** ([[graph-core]], `struct/domain_struct.h`) — RAM structs: `BaseNode` (type-erased adjacency) + `Node<T>` + `EdgeRef` + `ComplexRecord`.
- **POD** ([[pod-layout]], `struct/pod_struct.h`) — packed on-disk records (`#pragma pack(push,1)`).
- **ODT** ([[odt-layer]], `odt/`) — the ONLY layer allowed to convert Domain↔POD.
- **I/O** ([[persistence-io]], `io/`) — read/write paths, freelists, tombstoning.
- `Graph` (`graph.{h,cpp}`) owns the in-RAM map and drives persistence on every mutation.
- On-disk format is **packed, host-byte-order-dependent, with NO magic / version / checksum** — ABI-fragile. `main.cpp:24` wipes `db/` each run because a stale layout would silently corrupt reads.

## Relations

- **part-of** → [[Index]]
- **contains** → [[graph-core]]
- **contains** → [[pod-layout]]
- **contains** → [[odt-layer]]
- **contains** → [[persistence-io]]
- **relates-to** → [[glossary-load-bearing-typos]]
- **relates-to** → [[legacy-c-prototypes]]
- **documented-in** → docs/architecture/overview.md

## Sources

- `graph_core/graph.h`, `graph_core/struct/`, `graph_core/odt/`, `graph_core/io/`
