---
id: build-and-run
title: Build and run pointer_graphs
type: procedure
tags: [build, cmake]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Build and run pointer_graphs
> Configure and build with CMake plus Ninja on the MSYS2 ucrt64 toolchain, and run the binary from build/.

## When to run this

Any time you need to compile the engine or exercise a change - which, absent a test suite, is
also the only way to verify anything.

## Preconditions

- C++17 toolchain: MSYS2 `ucrt64` g++ 16 or newer, with `C:\msys64\ucrt64\bin` on `PATH`.
- CMake and Ninja available.
- If any POD layout changed, delete the stale `db/` first - the format is unversioned and a
  stale file is read as garbage.

## Steps

```powershell
cmake -S . -B build -G "Ninja"   # configure (re-runnable)
cmake --build build              # incremental build
.\build\graph.exe                # run - see the warning below
```

Clean rebuild: `Remove-Item -Recurse -Force build`, then repeat the two cmake commands.

## Verification

The binary is `main.cpp`, the six-phase [[smoke-test]]. Read its console output, then inspect
`db/`, `db/freelist/`, `db/attributes/`, `graph.log` and `graph_io.log`. Green means the build
succeeded **and** the smoke test round-tripped.

## If it goes wrong

- Nothing appears in `db/`, or reads fail: you almost certainly launched the binary from the
  wrong directory - see [[db-path-relative-to-cwd]].
- The window closes instantly: `main.cpp` ends with `system("pause")`, so run it from a
  terminal rather than double-clicking.
- Garbage reads after a struct change: delete `db/` and rerun.

## Links

- part of [[project-overview]] - the entry point for working with the repo
- relates to [[smoke-test]] - what the binary actually does when you run it
- relates to [[db-path-relative-to-cwd]] - the trap that most often breaks a run
