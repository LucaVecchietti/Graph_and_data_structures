---
node: legacy-c-prototypes
title: Out-of-Build C Prototypes
type: reference
tags: [legacy, c, dead-code]
updated: 2026-07-01
---

# Out-of-Build C Prototypes

Early C prototypes kept as historical reference — NOT part of the build.

## Facts

- `data_tructures/map_hash_table.{c,h}` (generic hash table) and `node_n_pointers.c` are **not in `CMakeLists.txt`** — not compiled, not linked.
- Known dead code: `hash_map_remove` is declared but not defined; `hash_map_reash` is a misnamed call site (both preserved as-is).
- Directory name `data_tructures` (no `s`) is load-bearing — see [[glossary-load-bearing-typos]].
- Don't wire these back into the build without an explicit ask.

## Relations

- **relates-to** → [[architecture-overview]]
- **relates-to** → [[glossary-load-bearing-typos]]
- **documented-in** → docs/modules/data_structures.md

## Sources

- `data_tructures/map_hash_table.c`, `data_tructures/map_hash_table.h`, `node_n_pointers.c`
