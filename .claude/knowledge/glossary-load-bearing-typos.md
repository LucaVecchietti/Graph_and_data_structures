---
node: glossary-load-bearing-typos
title: Load-Bearing Misspellings
type: glossary
tags: [glossary, gotcha]
updated: 2026-07-01
---

# Load-Bearing Misspellings

Names misspelled in source that MUST be preserved — renaming silently breaks unrelated code.

## Facts

- Directory **`data_tructures/`** (no `s`).
- Header **`costants.h`** (no `n`).
- Field **`neighborgs`** on `BaseNode` (`domain_struct.h:44`).
- In legacy/dead code: `hash_map_reash` (misnamed, see [[legacy-c-prototypes]]).

## Relations

- **relates-to** → [[architecture-overview]]
- **relates-to** → [[build-and-run]]
- **relates-to** → [[legacy-c-prototypes]]

## Sources

- `graph_core/costants.h`, `graph_core/struct/domain_struct.h:44`, `data_tructures/`
