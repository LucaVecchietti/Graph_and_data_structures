# Roadmap & Checkpoint

> Current capabilities of the pointer_graphs engine and the prioritized list of what is missing. Living document — update at each milestone.

| Campo | Valore |
|---|---|
| Tipo | roadmap |
| Lingua | en |
| Ultimo aggiornamento | 2026-06-13 |
| Commit di riferimento | cb9939c |
| Mirror | — |

---

## In one sentence

An **embedded, single-thread, append-only graph engine with space reclamation**: CRUD on (typed + COMPLEX) nodes and directed weighted edges, immediate persistence, BFS/DFS traversal, and a working freelist that reuses node, relation-list and edge-chunk regions. Missing: a query layer, on-disk durability/versioning, and a test suite.

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
- **Freelist** with exact-size segregated bins: slot+id reuse on `insert` (primitives and COMPLEX **per type**); every freed region (record, relation-list, edge chunk) is pushed onto the bins on `delete_node` and on `add_edge`, and the `rel`/`edges` bins are **reused** on edge rewrite (since 2026-06-13) — an `add_edge` that overwrites an existing weight reclaims the just-freed relation-list and edge chunks in place, so `nodes.dat`/`edges.dat` do not grow.
- **Reverse index** of inbound edges in RAM (rebuilt at load, delete in O(deg_in)).
- Consistent `meta` counters (`node_count`, `edge_count`, `free_count`, `free_edge_count`, monotonic ids).

## 🟡 Partial / half-done

| Area | State |
|---|---|
| Freelist **reuse** | All four bin families are reused: `nodes`/`complex` on `insert`, and `rel`/`edges` on edge rewrite (`update_node_edges`, since 2026-06-13) at exact fit. A weight-overwrite reclaims its holes in place → no growth. **Remaining boundary:** insert-time `write_relation_node_list` for a fresh node still appends, but it writes an empty rel-list + zero edges, so the growth is negligible (not yet full compaction of the insert path) |
| `add_edge` | Rewrites the **whole node** on every edge (no incremental append) |
| `traverse` | Does **not** lazy-load: only sees nodes already in RAM (`main.cpp` forces the load with an `add_edge "_load"` trick) |
| COMPLEX | The JSON attributes are an **opaque string**: no parsing/query over the fields |

## 🔴 TODO — what is missing

**DB features**
- [ ] **Edge attribute payloads (typed / "COMPLEX" edges)** — let a single edge carry a rich payload (a `type_label` + JSON attributes), mirroring the [COMPLEX node design](legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex). Today an `Edge` only carries `id / weight / to_node / from_node`. Planned shape:
  - **Out-of-line storage**, like COMPLEX nodes: the JSON attributes live in a sidecar file under `db/attributes/`; the edge stores only a reference to it. Reuse the existing machinery — a zero-padded `prog_number`, per-type size-class freelist bins, and the `json_prog.dat` free list for recycling.
  - **Edge-side header** analogous to `ComplexHeader` (e.g. `EdgeHeader { type_label_size, json_file_path_size }` + two length-prefixed strings), with `edge_*` ODT/IO helpers paralleling `complex_node_to_record` / `write_complex` / `read_complex`.
  - **Design tension to resolve first:** `edges.dat` stores **fixed-width 32-byte `Edge` records** in contiguous per-`(node, relation)` chunks, seeked via `edge_offset + i * 32`. A variable-width payload must stay **out-of-line** (the in-chunk `Edge` keeps its fixed size plus a reference — e.g. an attribute id/offset into a separate store — into the sidecar), otherwise the contiguous-chunk + seek model breaks. An "attributes-only-when-present" flag on the edge keeps plain edges at 32 bytes.
  - **Lifecycle parity:** the edge sidecar must be removed and its `prog_number` recycled when the edge is dropped (on `delete_node` and on a future single-edge delete), exactly as COMPLEX node deletion does today.
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
