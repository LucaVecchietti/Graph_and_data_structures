# Module: data_tructures

> Standalone C subtree containing a generic hash-table implementation. Currently not linked into the CMake build of `graph` — kept for reference and possible future reuse.

| Campo | Valore |
|---|---|
| Tipo | module |
| Lingua | en |
| Ultimo aggiornamento | 2026-05-26 |
| Commit di riferimento | 9e8b589 |
| Mirror | — |

> Note: the folder name is `data_tructures` (sic — missing `s`). Do not rename in docs; treat it as the canonical path.

---

## Overview

A generic chained hash table keyed by `const char*`, with `void*` values. Uses the djb2 hash function. Buckets are singly-linked lists of `Entry` nodes. Load factor over `0.75` triggers a doubling rehash (note the typo in the rehash function — see [BUG-006](../legacy/known_bugs.md)).

The table is referenced as a future direction in `node_n_pointers.c` (the prototype graph), where the author wrote a comment about migrating from raw pointer arrays to a hash-table-based adjacency. The C++ rewrite (`graph_core/`) skipped this hash table and went directly to `std::unordered_map`, which is why this file is now orphaned in the build.

## Struttura

```
data_tructures/
├── map_hash_table.h    Public types + function prototypes
└── map_hash_table.c    djb2 hash + put/get/init/free/rehash
```

## Design

- **Separate chaining** for collisions: each bucket is a linked list of `Entry`.
- **djb2** chosen for its simplicity and good distribution on short ASCII keys.
- **Doubling rehash** when load factor exceeds `0.75`. Buckets are moved by re-hashing each key against the new size.
- Keys are duplicated with `strdup` on insert — the table owns the key memory and frees it in `free_hash_table`. Values are NOT owned by the table.

See [Hash table standalone in C](../legacy/design_decisions.md#2026-05-26--hash-table-standalone-in-c-non-linkata).

## Tipi e strutture dati

### `typedef struct Entry` (`map_hash_table.h:4`)
| Field | Type | Purpose |
|---|---|---|
| `key` | `char*` | Owned copy of the key (allocated by `strdup`). |
| `value` | `void*` | Caller-owned value (the table does not free it). |
| `next` | `struct Entry*` | Next entry in the chain, or `NULL`. |

### `typedef struct HashTable` (`map_hash_table.h:11`)
| Field | Type | Purpose |
|---|---|---|
| `buckets` | `Entry**` | Array of `num_buckets` chain heads. |
| `num_buckets` | `int` | Current bucket count (doubled on rehash). |
| `size` | `int` | Declared in the struct but **never updated by `hash_map_put` / `hash_map_remove`**. The load-factor check uses `size / num_buckets`, but since `size` is never incremented, the rehash is never triggered in practice. See [BUG-007](../legacy/known_bugs.md). |

## Funzioni / interfacce esposte

### `HashTable* init_hash_table(int num_buckets)` (`map_hash_table.c:62`)
Allocates a `HashTable` and a zero-initialized `buckets` array of length `num_buckets`. Returns the new table.

- Side effects: two `malloc`/`calloc`.
- Failure mode: no `NULL` check on the allocations; if either fails, the next access will crash.

### `void hash_map_put(HashTable*, const char* key, void* value)` (`map_hash_table.c:20`)
Computes `bucket_index = djb2(key) % num_buckets`, allocates a new `Entry`, duplicates `key` via `strdup`, prepends to the bucket's chain. If `size / num_buckets > 0.75`, calls `hash_map_rehash` — **but no such symbol exists**; the function is named `hash_map_reash` at line 97. This is a compile-link bug if the threshold is ever crossed. See [BUG-006](../legacy/known_bugs.md).

### `void* hash_map_get(HashTable*, const char* key)` (`map_hash_table.c:41`)
Linear scan of the bucket chain comparing by `strcmp`. Returns the matching value or `NULL`.

### `void hash_map_remove(HashTable*, const char* key)` (`map_hash_table.h:23`)
**Declared in the header but not defined in the `.c` file.** See [BUG-008](../legacy/known_bugs.md).

### `void free_hash_table(HashTable*)` (`map_hash_table.c:75`)
Walks each bucket, frees every entry's `key` and the entry itself, then frees the bucket array and the table.

### `void hash_map_reash(HashTable*)` (`map_hash_table.c:97`)
Allocates a new bucket array of size `num_buckets * 2`, re-hashes each existing entry, replaces `buckets`, updates `num_buckets`. **Misnamed** — see [BUG-006](../legacy/known_bugs.md).

### `unsigned long hash_function(const char*)` (`map_hash_table.c:5`)
djb2: `hash = ((hash << 5) + hash) + c` starting from `5381`. Internal helper, not exposed in the header.

## Diagrammi

### Layout di un bucket

```
buckets
┌──────┐    ┌────────────────┐    ┌────────────────┐
│  [0] │──▶ │ key | val | ●─ │ ─▶ │ key | val | NUL│
├──────┤    └────────────────┘    └────────────────┘
│  [1] │── NULL
├──────┤
│  [2] │──▶ ...
├──────┤
│  ... │
└──────┘
```

### Insert path

```
hash_map_put(t, "road", v)
    │
    ▼
hash_function("road") % t->num_buckets  ─▶ bucket_index
    │
    ▼
new Entry { key = strdup("road"), value = v, next = t->buckets[i] }
    │
    ▼
t->buckets[i] = new Entry
    │
    ▼
if (size / num_buckets > 0.75)  ─▶ hash_map_rehash  ← symbol does not exist
```

## Dipendenze

**IN**: none in the current CMake build (the C target isn't linked into `graph`). `node_n_pointers.c` `#include`s the header but is itself not built.

**OUT**: `<stdlib.h>`, `<string.h>` only.

## Voci legacy collegate

- [Hash table standalone in C](../legacy/design_decisions.md#2026-05-26--hash-table-standalone-in-c-non-linkata)
- [BUG-006 — typo `hash_map_reash`](../legacy/known_bugs.md#2026-05-26--bug-006-typo-hash_map_reash)
- [BUG-007 — `size` mai aggiornato](../legacy/known_bugs.md#2026-05-26--bug-007-hashtablesize-mai-incrementato)
- [BUG-008 — `hash_map_remove` non implementata](../legacy/known_bugs.md#2026-05-26--bug-008-hash_map_remove-dichiarata-non-implementata)

## Riferimenti

- `data_tructures/map_hash_table.h:4` — `Entry`.
- `data_tructures/map_hash_table.h:11` — `HashTable`.
- `data_tructures/map_hash_table.c:5` — `hash_function` (djb2).
- `data_tructures/map_hash_table.c:20` — `hash_map_put`.
- `data_tructures/map_hash_table.c:97` — `hash_map_reash` (misnamed).
