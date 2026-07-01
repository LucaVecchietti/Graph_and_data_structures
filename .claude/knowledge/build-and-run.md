---
node: build-and-run
title: Build, Run & the Working-Directory Trap
type: workflow
tags: [build, cmake, ninja, workflow]
updated: 2026-07-01
---

# Build, Run & the Working-Directory Trap

How to compile and exercise the project, and the one trap that breaks persistence at runtime.

## Facts

- C++17, CMake + Ninja, MSYS2 `ucrt64` toolchain (g++ 16+); `C:\msys64\ucrt64\bin` must be on PATH.
- Configure + build: `cmake -S . -B build -G "Ninja"` then `cmake --build build`. Run: `.\build\graph.exe`.
- Clean rebuild: delete `build/`, re-run the two cmake commands.
- **No test suite.** `main.cpp` is a hand-driven smoke test (insert 3 nodes, add 2 edges, BFS), ends with `system("pause")` — run from a terminal, not by double-clicking.
- **Working-directory trap:** `DB_PATH` in `graph_core/costants.h` is the *relative* path `"../db"`; the store resolves to the repo's `db/` only when the binary runs from `build/` (a sibling of `db/`). Running from elsewhere reads/writes the wrong directory.

## Relations

- **relates-to** → [[persistence-io]]
- **relates-to** → [[glossary-load-bearing-typos]]
- **part-of** → [[Index]]

## Sources

- `CMakeLists.txt`, `main.cpp`, `graph_core/costants.h`
