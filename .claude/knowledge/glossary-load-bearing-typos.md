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
- Field **`neighborgs`** on `BaseNode`.
- Also present in bugs/dead code: `neihborgs` (BUG-004), `hash_map_reash` (legacy C prototype).

## Relations

- **relates-to** → [[architecture-overview]]
- **relates-to** → [[build-and-run]]
- **part-of** → [[Index]]

## Sources

- `graph_core/costants.h`, `graph_core/struct/domain_struct.h`, `data_tructures/`
