---
id: db-path-relative-to-cwd
title: DB_PATH is relative to the working directory
type: gotcha
tags: [disk, trap]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# DB_PATH is relative to the working directory
> DB_PATH is ../db, so the store only resolves correctly when the binary is launched from build/.

## Symptom

The engine appears to write nothing, reads fail, or a run silently starts from an empty
store - typically after launching `graph.exe` from the repo root, from an IDE, or by
double-clicking it.

## Root cause

`DB_PATH` in `graph_core/costants.h` is the **relative** path `"../db"`, and
`JSON_ATTR_PATH` is `"../db/attributes/"` in the same style. They resolve against the
process working directory, not the binary location, so the store only lands on the repo's
`db/` when the process runs from `build/` - or any other sibling of `db/`.

## Fix

Launch from `build/`:

```powershell
cd build; .\graph.exe
```

If a wrong-directory run already created a stray `db/` elsewhere, delete it - a partial store
in the wrong place is worse than none.

## How to avoid it

Treat "run from `build/`" as part of the run command, not a detail. The log files
(`graph.log`, `graph_io.log`) are also written relative to the working directory, so their
location is a quick tell that you launched from the wrong place.

## Links

- causes [[build-and-run]] - the single most common failure of that procedure
- relates to [[decision-json-sidecar-for-complex]] - the sidecar path is relative in the same way
