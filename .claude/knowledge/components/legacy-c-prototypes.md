---
id: legacy-c-prototypes
title: Out-of-build C prototypes
type: component
tags: [legacy, dead-code]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Out-of-build C prototypes
> data_tructures/ and node_n_pointers.c are early C prototypes, not in the build.

## Responsibility

Historical reference only. These are the early C prototypes that predate the C++ engine and
are kept in the repo for context.

## Where it lives

`data_tructures/map_hash_table.{c,h}` (a generic hash table) and `node_n_pointers.c` at the
repo root.

## How it behaves

- Neither file is listed in `CMakeLists.txt`: they are not compiled and not linked.
- They carry known dead code, preserved as-is - notably `hash_map_remove`, declared but never
  defined, and the misnamed call site `hash_map_reash`.

## Contracts and constraints

- Do not wire these into the build without an explicit request; they are not maintained
  alongside the engine.
- The directory name `data_tructures` (missing `s`) is load-bearing - see
  [[load-bearing-misspellings]].

## Links

- part of [[project-overview]] - repo content deliberately outside the engine
- relates to [[load-bearing-misspellings]] - the directory name is one of the misspellings
