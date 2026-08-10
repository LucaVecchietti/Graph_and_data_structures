# Roadmap & Checkpoint

> Current capabilities of the pointer_graphs engine and the prioritized list of what is missing. Living document — update at each milestone.

| Campo | Valore |
|---|---|
| Tipo | roadmap |
| Lingua | en |
| Ultimo aggiornamento | 2026-08-10 |
| Commit di riferimento | 7eaf864 (+ working tree: `ensure_loaded`, self-checking smoke test) |
| Mirror | — |

---

## In one sentence

An **embedded, single-thread graph engine with space reclamation**: CRUD on (typed + COMPLEX) nodes and directed weighted edges — including single-edge delete — immediate persistence, **O(1) edge add/overwrite** (chained fixed-width relation batches + edge linked lists), BFS/DFS traversal, and a working freelist that reuses node, relation-batch and single-edge regions. Missing: a query layer, on-disk durability/versioning, and a test suite.

## ✅ What it can do today

**Nodes**
- `insert<T>` for primitives (`int/float/double/char/bool`) and `COMPLEX` (`type_label` + JSON attributes in a sidecar file). Persisted immediately.
- `delete_node` is **complete**: removes the node, zeroes its bytes, tombstones the index slot, cleans up inbound edges, updates the counters, and handles the COMPLEX payload (sidecar removal + real record size).
- Lazy load on demand from disk (`read_node`) with type dispatch.

**Edges**
- `add_edge(start, end, relation, weight)` — directed, typed per relation, weighted, with a **globally stable id** that survives reloads and rewrites. Persisted in **O(1)** (since 2026-06-19): a new edge is appended + spliced at the relation's chain head and the one relation line is updated in place; the batch never moves so `nodes.idx` is untouched.
- Overwrite the weight of an existing edge (keeps the same id) — **O(1) in place** (`persist_edge_weight`), no growth.
- `delete_edge(start, end, relation)` and `delete_edge(edge_id)` — removes **one** edge, keeping the rest of the node intact. Both entry points share a single delete path: the id overload resolves `edge_id` → `(start, end, relation)` (in-RAM nodes first, else the O(N+E) `find_edge_by_id` disk scan, since no id → offset index exists) and then delegates to the first. **Not O(1)**: it goes through the whole-node `update_node_edges` rewrite — see the TODO below.

**Relations**
- A node's relations live in a **chain** of fixed-width batches (8 lines each), linked by `next_offset` with `head` 1→2→3. Since 2026-08-09 there is **no cap on relation types per node**: a full batch is extended by allocating a fresh one (freelist pop or append) and patching one 8-byte `next_offset` in place — `nodes.idx` is still never touched. Read, whole-node rewrite and delete all walk the chain, and every batch of a deleted chain goes back onto the `rel` bin.

**Traversal & space**
- `bfs` / `dfs` — policy-based (one template, queue/stack frontier). **Lazy-loading** since 2026-08-10: every node is materialised from disk as it is popped from the frontier (`Graph::ensure_loaded`, the single lazy-load path shared with `add_edge` / `delete_node` / `delete_edge`), so a walk of any depth works on a cold store. A node that cannot be read (never-assigned id, tombstoned slot) throws instead of being silently skipped.
- **Freelist** with exact-size segregated bins: slot+id reuse on `insert` (primitives and COMPLEX **per type**); every freed region (node record, relation batch, edge) is pushed onto the bins on `delete_node` / `update_node_edges`. Sizes are now standardized: one `rel` bin (2213 B batch) and one `edges` bin (48 B single edge) — a freed edge slot is reused by the next `add_edge`. A weight overwrite is O(1) in place → no file growth.
- **Reverse index** of inbound edges in RAM (rebuilt at load, delete in O(deg_in)). It is keyed **node → node**, with no relation granularity: a source stays in a target's inbound set as long as *any* of its relations still points there (`delete_edge` checks this before dropping it — getting it wrong left edges dangling at tombstoned slots).
- Consistent `meta` counters (`node_count`, `edge_count`, `free_count`, `free_edge_count`, monotonic ids).

## 🟡 Partial / half-done

| Area | State |
|---|---|
| Freelist **reuse** | All bin families are reused: `nodes`/`complex` on `insert`, `rel` (single 2213 B class) and `edges` (single 48 B class) on `add_edge` / `update_node_edges`. Since 2026-06-19 standard sizes mean one bin file per struct type. **Remaining boundary:** insert-time `write_relation_node_list` for a fresh node still appends an empty batch (negligible growth) |
| Relation batches | Chained since 2026-08-09, so the 8-types cap is gone. **Remaining boundaries:** a batch left empty by a shrink is only reclaimed by the whole-node rewrite (`update_node_edges`), never by `persist_new_edge`; and `RELATION_LINES_PER_BATCH` stays at 8, so the on-disk floor is still 2213 B per node (lowering it is a schema break) |
| Single-edge delete | Both `delete_edge` overloads work and keep RAM, disk and the reverse index consistent, but they ride the whole-node rewrite: O(deg) and `edges.dat` grows per call. `delete_edge(edge_id)` resolves the id by scanning (RAM, then O(N+E) on disk) because no `edge_id` → offset index exists |
| `traverse` | Lazy-loads since 2026-08-10, so no caller needs the old `add_edge "_load"` trick. **Remaining boundary:** a walk materialises the whole reachable component and `nodes` has **no eviction**, so RAM only ever grows for the lifetime of the `Graph` |
| COMPLEX | The JSON attributes are an **opaque string**: no parsing/query over the fields |

## 🔴 TODO — what is missing

**DB features**
- [x] **Relation batch chaining** — done 2026-08-09: a 2nd+ batch via `next_offset` (`head` 1→2→3) lifted the 8-relation-types-per-node cap. See the [decision](legacy/design_decisions.md#2026-08-09--relation-batch-chaining-catena-di-batch-via-next_offset); guarded by `main.cpp` Phase 7.
- [ ] **Delete a single edge in O(1)** — the API now **exists** (`delete_edge`, both overloads), but it is implemented on top of the whole-node `update_node_edges` rewrite: cost is O(total degree of the node) and `edges.dat` grows by `48 * surviving edges` per call, because the rewrite re-appends every relation's edge run at EOF while the freed 48 B slots sit on the `edges` bin waiting for the next `add_edge`. What is missing is the O(1) path: unlink the edge from its chain (patch the neighbours' `prev_offset`/`next_offset`), free its slot, and decrement `edge_count` on that one relation line in place. For the id overload it would also mean an `edge_id` → offset index, to replace today's O(N+E) scan.
- [ ] **Edge attribute payloads (typed / "COMPLEX" edges)** — let a single edge carry a rich payload (a `type_label` + JSON attributes), mirroring the [COMPLEX node design](legacy/design_decisions.md#2026-05-26--storage-sidecar-json-per-nodi-complex). Today an `Edge` carries `id / weight / to_node / from_node / prev_offset / next_offset`. Planned shape:
  - **Out-of-line storage**, like COMPLEX nodes: the JSON attributes live in a sidecar file under `db/attributes/`; the edge stores only a reference to it. Reuse the existing machinery — a zero-padded `prog_number`, per-type size-class freelist bins, and the `json_prog.dat` free list for recycling.
  - **Edge-side header** analogous to `ComplexHeader` (e.g. `EdgeHeader { type_label_size, json_file_path_size }` + two length-prefixed strings), with `edge_*` ODT/IO helpers paralleling `complex_node_to_record` / `write_complex` / `read_complex`.
  - **Design tension to resolve first:** `edges.dat` stores **fixed-width 48-byte `Edge` records** chained per `(node, relation)` via `prev_offset`/`next_offset` (since 2026-06-19). A variable-width payload must stay **out-of-line** (the `Edge` keeps its fixed size plus a reference — e.g. an attribute id/offset into a separate store — into the sidecar), otherwise the fixed-width + chain-walk model breaks. An "attributes-only-when-present" flag on the edge keeps plain edges at 48 bytes.
  - **Lifecycle parity:** the edge sidecar must be removed and its `prog_number` recycled when the edge is dropped (on `delete_node` and on a future single-edge delete), exactly as COMPLEX node deletion does today.
- [ ] **Update** a node's payload in place (today only delete + insert)
- [ ] Query layer: filters, attribute search, traversal with predicates
- [x] Lazy-load inside `traverse` (drop the `_load` trick) — done 2026-08-10: `ensure_loaded` on every frontier pop; guarded by `main.cpp` Phase 2, which walks a 2-hop chain from a cold store.
- [ ] Undirected edges / exposed reverse queries

**Format robustness**
- [ ] Magic + version + checksum in the files (today **none** → a POD change silently corrupts)
- [ ] Endianness independence (today host-byte-order, x86-64 only)
- [ ] Crash safety / transactions / WAL (writes are not atomic)

**Infrastructure**
- [ ] **No test suite** (only the `main.cpp` smoke test). Since 2026-08-10 it self-checks: every phase states its expectations, and the run ends with a verdict plus a non-zero exit code on failure — no more log eyeballing. Phases 1-7 cover insert / cold-reload traversal / delete+reuse / COMPLEX / compaction / edge chains / relation-batch chaining. **`delete_edge` is not covered by any phase** — both overloads and the reverse-index granularity were verified with a throwaway harness, so there is no regression guard for them.
- [ ] Real CLI / API (today `main.cpp` is a hand-driven bench)
- [ ] Thread safety / concurrency (logger has no mutex, no locking)

## ⚠️ Known fragilities

- On-disk format is ABI-fragile and unversioned → **start from a clean `db/`** after any layout change (the 2026-06-19 `NodeRelationList`/`Edge` layout change is one such break).
- Fixed-width relation batches cost **~2.2 KB per 8 relation types per node** on disk regardless of how few relations are used (the tail is always written full-width), so a node with 9 types pays two full batches; `Edge` is 48 B. The price of in-place line addressing.
- A relation chain is walked by `next_offset` with **no checksum** to validate it: a corrupted offset is caught only by the `RELATION_MAX_BATCHES` (4096) bound, which throws.
- Any whole-node rewrite (`update_node_edges`, so `delete_node`'s inbound cleanup and both `delete_edge` overloads) **appends** the surviving edge runs at EOF of `edges.dat` and leaves the freed 48 B slots on the `edges` bin. `nodes.dat` does not grow (the batches are reused in place) but `edges.dat` does, until a later `add_edge` drains the bin.
- `data_tructures/` (C hash table) is **not linked** — its internal bugs are fixed but it remains reference code.
- `DB_PATH = "../db"` is relative → run **from `build/`** or the wrong directory is read/written.

## Related

- [architecture/overview.md](architecture/overview.md) — system components and data flow.
- [modules/graph_core.md](modules/graph_core.md) — the engine internals.
- [modules/db.md](modules/db.md) — the on-disk format.
- [legacy/known_bugs.md](legacy/known_bugs.md) — bug log (all `BUG-NNN` currently closed).
