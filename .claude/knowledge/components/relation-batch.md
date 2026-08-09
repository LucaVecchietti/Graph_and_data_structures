---
id: relation-batch
title: Fixed-width relation batch
type: component
tags: [disk-format, relations]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Fixed-width relation batch
> NodeRelationList: a constant 2213-byte batch of up to 8 relation lines, addressable in place and chainable.

## Responsibility

Store a node's relations in a constant-size, fixed-width batch so that one relation's
`edge_offset`/`edge_count` can be updated with a single direct seek-write.

## Where it lives

`NodeRelationList` in `graph_core/struct/pod_struct.h`; the width constants in
`graph_core/costants.h`; the read/write helpers in `graph_core/io/graph_io.h`
(`write_relation_line`, `read_relation_line`, `pad_relation_tail`).

## How it behaves

- One batch = a 37-byte header plus a fixed `RELATION_BATCH_TAIL` of 2176 bytes, so the
  region is always **2213 bytes**, no matter how many relations are actually used.
- The tail holds up to `RELATION_LINES_PER_BATCH = 8` lines of `RELATION_LINE_SIZE = 272`
  bytes each: `[edge_offset(8)][edge_count(8)][name_len(1)][name[255]]`.
- Line `i` sits at `tail + i * 272`, so updating one relation is an O(1) seek-write. This is
  the foundation of the O(1) `add_edge` in [[decision-o1-add-edge]].
- Header fields: `node_id`, `type_count` (0..8 in use), `batch_size` (2176), `free_bytes`,
  `next_offset` (next batch of this node's chain, 0 if last), `head` (serial 1, 2, 3...),
  `is_deleted`.
- Because the region size is constant, the `rel` [[freelist]] needs exactly one size class.
- A node with more than 8 relation types owns a **chain** of batches: `ceil(types / 8)` of
  them, linked by `next_offset`. The batches are allocated independently, so they are not
  necessarily contiguous in `nodes.dat`. See [[decision-relation-batch-chaining]].

## Contracts and constraints

- Unused lines are zero-filled, so every node pays ~2.2 KB on disk per started group of 8
  relation types, regardless of fanout. That is the deliberate price of in-place line
  addressing.
- Lines `0..type_count-1` of a batch are used, with no holes: a new relation type goes into
  the last batch of the chain, or into a fresh one appended to it.
- The layout helpers `relation_batch_region_size()` (2213), `relation_batch_count(n)` and
  `relation_lines_in_batch(n, b)` in `io/graph_io.h` are the only place that computes the
  chain geometry.

## Links

- part of [[pod-layout]] - one of the on-disk records
- relates to [[edge-record]] - each line points at the head of an edge chain
- implements [[decision-fixed-width-relation-batch]] - the format this decision froze
- implements [[decision-relation-batch-chaining]] - why a batch is one link of a chain
