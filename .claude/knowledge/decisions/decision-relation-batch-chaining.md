---
id: decision-relation-batch-chaining
title: Relation batch chaining
type: decision
tags: [disk-format, relations]
aliases: [eight relation types cap]
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Relation batch chaining
> A node's relation list is a chain of fixed-width batches linked by next_offset, so nothing caps the relation types per node.

## Context

[[decision-fixed-width-relation-batch]] froze a tail of 8 relation lines and reserved
`next_offset` + `head` in the header for continuing the list, but never implemented the
continuation. A node's ninth relation type therefore threw in three places
(`write_relation_node_list`, `persist_new_edge`, `update_node_edges`). It was the residual
boundary of that decision and the first open roadmap item.

## Options considered

- **Append the new line to the LAST batch of the chain, allocating a fresh batch when it is
  full** - chosen. Keeps the "lines 0..type_count-1 used, no holes" invariant.
- **First-fit a free line anywhere in the chain** - only pays off if some path removes a
  single relation line and leaves a hole. Nothing does today (delete rewrites the whole
  node), so it buys a walk and hole bookkeeping for nothing.
- **Shrink `RELATION_LINES_PER_BATCH` to 4 now that chaining exists**, to halve the ~2.2 KB
  per-node floor - deferred: it is a schema break and changes the `rel` bin's size class.
- **Force a chain to be one contiguous `k * 2213` region** - trivial to walk, but needs one
  freelist size class per `k` and a full-chain rewrite to add a type, which kills the O(1)
  add.

## Decision

`NodeIndex.relation_offset` points at the first batch; each header's `next_offset` points at
the following one (0 = last); `head` numbers them 1, 2, 3... A node with `n` relation types
owns `ceil(n / 8)` batches, minimum one. **No POD layout changed** - both fields already
existed and an old `db/` has `next_offset = 0` everywhere, i.e. a chain of length 1 - so this
is not a schema break.

- Batches of one chain are allocated independently ([[freelist]] pop, else append), so they
  are **not necessarily contiguous**: only `next_offset` defines the order.
- `persist_new_edge` walks the chain to find the relation line, and when the last batch is
  full it allocates a fresh one and links it with a single 8-byte in-place write to the
  previous header's `next_offset`. Nothing moves, so `nodes.idx` is still never touched.
- `read_relation_node_list` walks the chain and can report every batch offset it visited,
  which is what lets the reclaim paths free a whole chain.
- Every walk is bounded by `RELATION_MAX_BATCHES` (4096) and throws past it: the format has
  no checksum, so a corrupted `next_offset` must not loop forever.

## Consequences

- The cap on relation types per node is gone; the practical ceiling is
  `RELATION_MAX_BATCHES * 8` = 32768 types.
- `persist_new_edge` becomes O(batches) = O(types / 8) instead of strictly O(1). For a node
  with 8 or fewer types the cost is unchanged (one header read).
- Disk cost per node is now `ceil(types / 8) * 2213` bytes: 9 types pay two full batches.
- `delete_node_from_disk` and `update_node_edges` push **one** `rel` freelist record per
  batch, not one per node, and `update_node_edges` must decide where every batch of the new
  chain lands *before* writing any of it - `next_offset` needs the following offset, and
  probing EOF twice without writing hands back the same offset.
- Watch for: a batch left empty by a shrink is only reclaimed by the whole-node rewrite, never
  by `persist_new_edge`.

## Links

- relates to [[decision-fixed-width-relation-batch]] - closes the batch-chaining boundary that decision left open, without changing its format
- specifies [[relation-batch]] - the chain the batch now lives in
- relates to [[decision-o1-add-edge]] - extends that O(1) write path past a full batch
- depends on [[freelist]] - a chained batch is allocated from the single 2213-byte rel bin
- specifies [[persistence-io]] - the read/write/reclaim paths that walk the chain
- verified by [[smoke-test]] - Phase 7 drives a node past two batch boundaries
