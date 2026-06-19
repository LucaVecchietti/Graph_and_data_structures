# Roadmap & Checkpoint

> Current capabilities of the pointer_graphs engine and the prioritized list of what is missing. Living document — update at each milestone.

| Campo | Valore |
|---|---|
| Tipo | roadmap |
| Lingua | en |
| Ultimo aggiornamento | 2026-06-19 |
| Commit di riferimento | 0a043f7 |
| Mirror | — |

---

## In one sentence

An **embedded, single-thread graph engine with space reclamation**: CRUD on (typed + COMPLEX) nodes and directed weighted edges, immediate persistence, **O(1) edge add/overwrite** (fixed-width relation batches + edge linked lists), BFS/DFS traversal, and a working freelist that reuses node, relation-batch and single-edge regions. Missing: a query layer, on-disk durability/versioning, and a test suite.

## ✅ What it can do today

**Nodes**
- `insert<T>` for primitives (`int/float/double/char/bool`) and `COMPLEX` (`type_label` + JSON attributes in a sidecar file). Persisted immediately.
- `delete_node` is **complete**: removes the node, zeroes its bytes, tombstones the index slot, cleans up inbound edges, updates the counters, and handles the COMPLEX payload (sidecar removal + real record size).
- Lazy load on demand from disk (`read_node`) with type dispatch.

**Edges**
- `add_edge(start, end, relation, weight)` — directed, typed per relation, weighted, with a **globally stable id** that survives reloads and rewrites. Persisted in **O(1)** (since 2026-06-19): a new edge is appended + spliced at the relation's chain head and the one relation line is updated in place; the batch never moves so `nodes.idx` is untouched.
- Overwrite the weight of an existing edge (keeps the same id) — **O(1) in place** (`persist_edge_weight`), no growth.

**Traversal & space**
- `bfs` / `dfs` — policy-based (one template, queue/stack frontier).
- **Freelist** with exact-size segregated bins: slot+id reuse on `insert` (primitives and COMPLEX **per type**); every freed region (node record, relation batch, edge) is pushed onto the bins on `delete_node` / `update_node_edges`. Sizes are now standardized: one `rel` bin (2213 B batch) and one `edges` bin (48 B single edge) — a freed edge slot is reused by the next `add_edge`. A weight overwrite is O(1) in place → no file growth.
- **Reverse index** of inbound edges in RAM (rebuilt at load, delete in O(deg_in)).
- Consistent `meta` counters (`node_count`, `edge_count`, `free_count`, `free_edge_count`, monotonic ids).

## 🟡 Partial / half-done

| Area | State |
|---|---|
| Freelist **reuse** | All bin families are reused: `nodes`/`complex` on `insert`, `rel` (single 2213 B class) and `edges` (single 48 B class) on `add_edge` / `update_node_edges`. Since 2026-06-19 standard sizes mean one bin file per struct type. **Remaining boundary:** insert-time `write_relation_node_list` for a fresh node still appends an empty batch (negligible growth) |
| Relation batches | Fixed-width, one batch of 8 lines per node. **Cap of 8 relation types per node**: a 2nd batch via `next_offset` (chaining) is not implemented — exceeding 8 throws (`persist_new_edge` / `update_node_edges`) |
| `traverse` | Does **not** lazy-load: only sees nodes already in RAM (`main.cpp` forces the load with an `add_edge "_load"` trick) |
| COMPLEX | The JSON attributes are an **opaque string**: no parsing/query over the fields |

## 🔴 TODO — what is missing

**DB features**
- [ ] **Relation batch chaining** — a 2nd+ batch via `next_offset` (`head` 1→2→3) to lift the 8-relation-types-per-node cap. `persist_new_edge` / `update_node_edges` currently throw when the batch is full.
- [ ] **Delete a single edge in O(1)** — unlink from the chain + free the slot + decrement the relation line, instead of the whole-node `update_node_edges` rewrite used by `delete_node`'s inbound cleanup today.
- [ ] **Edge attribute payloads (typed / "COMPLEX" edges)** — let a single edge carry a rich payload (a `type_label` + JSON attributes), mirroring the [COMPLEX node design](legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex). Today an `Edge` carries `id / weight / to_node / from_node / prev_offset / next_offset`. Planned shape:
  - **Out-of-line storage**, like COMPLEX nodes: the JSON attributes live in a sidecar file under `db/attributes/`; the edge stores only a reference to it. Reuse the existing machinery — a zero-padded `prog_number`, per-type size-class freelist bins, and the `json_prog.dat` free list for recycling.
  - **Edge-side header** analogous to `ComplexHeader` (e.g. `EdgeHeader { type_label_size, json_file_path_size }` + two length-prefixed strings), with `edge_*` ODT/IO helpers paralleling `complex_node_to_record` / `write_complex` / `read_complex`.
  - **Design tension to resolve first:** `edges.dat` stores **fixed-width 48-byte `Edge` records** chained per `(node, relation)` via `prev_offset`/`next_offset` (since 2026-06-19). A variable-width payload must stay **out-of-line** (the `Edge` keeps its fixed size plus a reference — e.g. an attribute id/offset into a separate store — into the sidecar), otherwise the fixed-width + chain-walk model breaks. An "attributes-only-when-present" flag on the edge keeps plain edges at 48 bytes.
  - **Lifecycle parity:** the edge sidecar must be removed and its `prog_number` recycled when the edge is dropped (on `delete_node` and on a future single-edge delete), exactly as COMPLEX node deletion does today.
- [ ] **Update** a node's payload in place (today only delete + insert)
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

- On-disk format is ABI-fragile and unversioned → **start from a clean `db/`** after any layout change (the 2026-06-19 `NodeRelationList`/`Edge` layout change is one such break).
- Fixed-width relation batch costs **~2.2 KB per node** on disk regardless of how few relations it has (the tail is always written full-width); `Edge` is 48 B. The price of in-place line addressing.
- **Hard cap of 8 relation types per node** until batch chaining lands — exceeding it throws.
- `data_tructures/` (C hash table) is **not linked** — its internal bugs are fixed but it remains reference code.
- `DB_PATH = "../db"` is relative → run **from `build/`** or the wrong directory is read/written.

## Related

- [architecture/overview.md](architecture/overview.md) — system components and data flow.
- [modules/graph_core.md](modules/graph_core.md) — the engine internals.
- [modules/db.md](modules/db.md) — the on-disk format.
- [legacy/known_bugs.md](legacy/known_bugs.md) — bug log (all `BUG-NNN` currently closed).
