# Roadmap & Checkpoint

> Current capabilities of the pointer_graphs engine and the prioritized list of what is missing. Living document — update at each milestone.

| Campo | Valore |
|---|---|
| Tipo | roadmap |
| Lingua | en |
| Ultimo aggiornamento | 2026-06-07 |
| Commit di riferimento | 9966603 |
| Mirror | — |

---

## In one sentence

An **embedded, single-thread, append-only graph engine with space reclamation**: CRUD on (typed + COMPLEX) nodes and directed weighted edges, immediate persistence, BFS/DFS traversal, and a working node freelist. Missing: a query layer, on-disk durability/versioning, a test suite, and edge-space reuse.

## ✅ What it can do today

**Nodes**
- `insert<T>` for primitives (`int/float/double/char/bool`) and `COMPLEX` (`type_label` + JSON attributes in a sidecar file). Persisted immediately.
- `delete_node` is **complete**: removes the node, zeroes its bytes, tombstones the index slot, cleans up inbound edges, updates the counters, and handles the COMPLEX payload (sidecar removal + real record size).
- Lazy load on demand from disk (`read_node`) with type dispatch.

**Edges**
- `add_edge(start, end, relation, weight)` — directed, typed per relation, weighted, with a **globally stable id** that survives reloads and rewrites. Persisted.
- Overwrite the weight of an existing edge (keeps the same id).

**Traversal & space**
- `bfs` / `dfs` — policy-based (one template, queue/stack frontier).
- **Freelist** with exact-size segregated bins: slot+id reuse on `insert` (primitives and COMPLEX **per type**); every freed region (record, relation-list, edge chunk) is pushed onto the bins on `delete_node` and on `add_edge`.
- **Reverse index** of inbound edges in RAM (rebuilt at load, delete in O(deg_in)).
- Consistent `meta` counters (`node_count`, `edge_count`, `free_count`, `free_edge_count`, monotonic ids).

## 🟡 Partial / half-done

| Area | State |
|---|---|
| Freelist **reuse** | `nodes`/`complex` bins are **reused**; `rel`/`edges` bins are populated and tracked but **not reused yet** → `edges.dat`/`nodes.dat` still grow |
| `add_edge` | Rewrites the **whole node** on every edge (no incremental append) |
| `traverse` | Does **not** lazy-load: only sees nodes already in RAM (`main.cpp` forces the load with an `add_edge "_load"` trick) |
| COMPLEX | The JSON attributes are an **opaque string**: no parsing/query over the fields |

## 🔴 TODO — what is missing

**DB features**
- [ ] **Reuse of the `rel`/`edges` bins** (edge-space compaction) — the natural next freelist step
- [ ] **Update** a node's payload in place (today only delete + insert)
- [ ] **Delete a single edge** (today only via `delete_node`)
- [ ] Query layer: filters, attribute search, traversal with predicates
- [ ] Lazy-load inside `traverse` (drop the `_load` trick)
- [ ] Undirected edges / exposed reverse queries

**Format robustness**
- [ ] Magic + version + checksum in the files (today **none** → a POD change silently corrupts)
- [ ] Endianness independence (today host-byte-order, x86-64 only)
- [ ] Crash safety / transactions / WAL (writes are not atomic)

**Infrastructure**
- [ ] **No test suite** (only the `main.cpp` smoke test with visual log comparison)
- [ ] Real CLI / API (today `main.cpp` is a hand-driven bench)
- [ ] Thread safety / concurrency (logger has no mutex, no locking)

## ⚠️ Known fragilities

- On-disk format is ABI-fragile and unversioned → **start from a clean `db/`** after any layout change.
- `data_tructures/` (C hash table) is **not linked** — its internal bugs are fixed but it remains reference code.
- `DB_PATH = "../db"` is relative → run **from `build/`** or the wrong directory is read/written.

## Related

- [architecture/overview.md](architecture/overview.md) — system components and data flow.
- [modules/graph_core.md](modules/graph_core.md) — the engine internals.
- [modules/db.md](modules/db.md) — the on-disk format.
- [legacy/known_bugs.md](legacy/known_bugs.md) — bug log (all `BUG-NNN` currently closed).
