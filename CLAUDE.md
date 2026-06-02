# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Run

C++17 project built with CMake + Ninja. Targets Windows with the MSYS2 `ucrt64` toolchain (g++ 16+); `C:\msys64\ucrt64\bin` must be on PATH.

```powershell
# Configure + build (re-runnable; incremental)
cmake -S . -B build -G "Ninja"
cmake --build build

# Run
.\build\graph.exe
```

Clean rebuild: `Remove-Item -Recurse -Force build` then re-run the two cmake commands.

There is **no test suite**. `main.cpp` is a hand-driven smoke test (insert 3 nodes, add 2 edges, BFS) and ends with `system("pause")` — run from a terminal, not by double-clicking, or the window closes before you see the output.

### Working-directory trap

`DB_PATH` in `graph_core/costants.h` is the **relative** path `"../db"`. The store resolves to the repo's `db/` only when the binary is launched from `build/` (or any sibling of `db/`). Running from elsewhere will read/write the wrong directory or fail. The `db/*` contents are gitignored — schema-breaking changes to POD layouts should be paired with deleting the stale files in `db/`.

## Architecture

Three-layer separation inside `graph_core/`, deliberately kept apart so RAM and disk shapes can evolve independently:

1. **Domain** (`struct/domain_struct.h`) — `BaseNode` (type-erased, holds the adjacency map) + `template<class T> Node : BaseNode` (typed payload). Adjacency is `unordered_map<string relation, unordered_map<int neighbor_id, pair<int weight, BaseNode*>>>`.
2. **POD** (`struct/pod_struct.h`) — packed (`#pragma pack(push,1)`) on-disk records: `NodeIndex`, `NodeRecord<T>`, `RelationNodeList`, `Edge`, `MetaRecord`, plus the WIP `ComplexHeader`. No magic, no version, no checksum — the on-disk format is host-byte-order-dependent and ABI-fragile.
3. **ODT** (`odt/`) — Object Data Transfer: the only layer allowed to convert between (1) and (2). `node_to_record`, `node_to_relation_list`, `reconstruct_neighbors`, `edge_to_pod`.

`Graph` (`graph.h/cpp`) owns the in-RAM map `unordered_map<int, BaseNode*>` and `MetaRecord meta`. Every `insert<T>` immediately persists via `io/graph_io.h`'s `write_node` (record → relations → index, all appended; `meta.dat` is truncated and rewritten).

Traversal is policy-based: `Graph::traverse<Policy>` is one template, `BFSPolicy`/`DFSPolicy` (`struct/functions_policies.h`) differ only in `Frontier = queue` vs `stack`. `bfs`/`dfs` are thin wrappers.

`T` is mapped to its on-disk `NodeType` tag via the compile-time map `node_type_of<T>` in `struct/type_registry.h`. Only the specializations there are valid payloads — `int, float, double, char, bool` (and `ComplexRecord`, WIP).

### Persistence quirks to know before editing

- **`add_edge` is RAM-only.** It does not re-flush `edges.dat` after the initial `write_node`. Restarting loses every edge added after insert. (Tracked as BUG-001 in `docs/legacy/known_bugs.md`.)
- **`read_node` leaves neighbor pointers as `nullptr`.** After loading a node from disk, its neighbors in the adjacency map are unlinked — anything traversing must re-resolve.
- **`reconstruct_neighbors` is a stub** that returns an empty map (BUG-003).
- **`node_form_pod` has a typo** referencing `node.neihborgs` (BUG-004) and currently does not compile if instantiated.
- Append-only writes to `nodes.dat` / `nodes.idx` / `edges.dat`; `meta.dat` is fully rewritten each time. Fixed-width `NodeIndex` (25 bytes) gives O(1) lookup via `seekg(id * 25)`.

### Spellings that look like typos but are load-bearing

These are misspelled in source and must be preserved when editing — renaming silently breaks unrelated code:
- Directory **`data_tructures/`** (no `s`).
- Header **`costants.h`** (no `n`).
- Field **`neighborgs`** on `BaseNode`.

### Out-of-build legacy code

`data_tructures/map_hash_table.{c,h}` and `node_n_pointers.c` are early C prototypes — they are **not** in `CMakeLists.txt`, not linked, and have known dead code (e.g. `hash_map_remove` declared but not defined, `hash_map_reash` misnamed call site). Treat as historical reference; don't wire them back in without an explicit ask.

### `COMPLEX` payload (WIP)

`NodeType::COMPLEX = 255` and `ComplexRecord { string type_label; string json_attributes; }` exist in the type registry, but the I/O path is not implemented: `ComplexRecord` is not trivially copyable, so `Graph::insert<ComplexRecord>` currently fails the `static_assert` in `node_to_record`. A dedicated write path (using `ComplexHeader` + two length-prefixed strings) is the open work item.

## Documentation

`docs/` is the canonical place for any architectural / module / decision write-ups. It has structure and a documentation standard — read `docs/STANDARD.md` before adding or modifying a doc. The index is `docs/README.md`.

The `docs-keeper` skill is responsible for maintaining `docs/`. Invoke it (or follow the standard manually) when:
- changing `graph_core/struct/`, `policies`, `io/`, `odt/`, or anything affecting the on-disk format,
- adding a new module under `graph_core/`, `data_tructures/`, or `db/`,
- the user asks to "documenta" / "aggiorna i docs" / "traccia".

Legacy logs live under `docs/legacy/`:
- `design_decisions.md` — why something is the way it is,
- `api_changes.md` — before/after of signature/behavior changes,
- `known_bugs.md` — `BUG-NNN` IDs (sequential, never reused); cite them in commit messages and PRs.

User-facing docs are primarily Italian-flavored English; the Italian mirror under `docs/it/` is created only on explicit request. Per `STANDARD.md`, every doc carries a metadata table (Tipo / Lingua / Ultimo aggiornamento / Commit di riferimento / Mirror) — keep `Ultimo aggiornamento` as an **absolute** date and update `Commit di riferimento` to the short SHA the doc reflects.
