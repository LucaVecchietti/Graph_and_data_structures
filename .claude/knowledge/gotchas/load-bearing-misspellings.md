---
id: load-bearing-misspellings
title: Load-bearing misspellings
type: gotcha
tags: [naming, trap]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Load-bearing misspellings
> data_tructures, costants.h and neighborgs are misspelled in source and must not be corrected.

## Symptom

An innocuous-looking rename - fixing an obvious typo in a directory, header or field name -
breaks the build or, worse, silently breaks unrelated code that still uses the old spelling.

## Root cause

Several names are misspelled at their point of definition and used consistently that way
across the codebase. They are effectively part of the API:

- the directory `data_tructures/` (missing `s`)
- the header `costants.h` (missing `n`)
- the field `neighborgs` on `BaseNode`
- in the out-of-build C prototypes, the misnamed call site `hash_map_reash`

## Fix

Do not correct them in passing. If one genuinely must be renamed, do it as a dedicated,
repo-wide change - never as a drive-by cleanup inside another task.

## How to avoid it

When writing code that touches these names, copy the existing spelling rather than typing what
looks right. `CLAUDE.md` lists them for the same reason.

## Links

- relates to [[legacy-c-prototypes]] - `data_tructures/` and `hash_map_reash` live there
- relates to [[graph-class]] - `neighborgs` is a field on BaseNode
- part of [[project-overview]] - a project-wide convention trap
