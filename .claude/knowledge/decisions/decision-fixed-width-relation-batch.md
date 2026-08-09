---
id: decision-fixed-width-relation-batch
title: Fixed-width relation batch and doubly-linked edges
type: decision
tags: [disk-format, perf]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Fixed-width relation batch and doubly-linked edges
> Freeze relations into a constant-size batch of fixed-width lines and chain edges, to make single-relation updates addressable in place.

## Context

`add_edge` was O(deg): `update_node_edges` rewrote a node's entire relation list plus **all**
of its edge chunks for every single edge added. The earlier edge-persistence design had
explicitly rejected an edge linked list in order to keep contiguous chunks per
`(node, relation)`, accepting the write amplification. To reach O(1) the format needed to
allow (a) updating one relation's line without touching the others, and (b) attaching an edge
without rewriting its chunk.

## Options considered

- **Variable-width tail updated in place** (the pre-existing format) - impossible without
  shifting the file tail; this was the root cause of the O(deg).
- **Fixed-capacity pre-allocation per (node, relation)** - wasteful and inflates the primary
  file under skewed fanout.
- **Linked list of edges but a still-variable relation tail** - would leave adding a *new*
  relation type O(number of types).

## Decision

Freeze a new on-disk format (2026-06-19): `NodeRelationList` becomes a 37-byte header plus a
fixed 2176-byte tail of up to 8 lines of 272 bytes, so a batch region is a constant 2213
bytes and line `i` is addressable at `tail + i * 272`. `Edge` grows from 32 to 48 bytes with
`prev_offset`/`next_offset`, making the edges of a `(node, relation)` pair a doubly-linked
list. Shared format helpers (`write_relation_line`, `read_relation_line`, `pad_relation_tail`,
`write_edge_chain_at`) centralise the layout so read and write cannot drift apart.

## Consequences

- Schema break: any pre-existing `db/` must be deleted.
- Fixed space cost of ~2.2 KB of relation batch per node, however few relations it has, and a
  50% larger `Edge`. That is the price of in-place line addressing.
- A constant batch size collapses the `rel` freelist to a single size class, and the scattered
  splice collapses `edges` to a single 48-byte class.
- Left open at the time: exploiting the format for an O(1) `add_edge`, resolved by
  [[decision-o1-add-edge]]; and batch chaining beyond 8 relation types, still open.

## Links

- specifies [[relation-batch]] - the format this decision froze
- specifies [[edge-record]] - the chained 48-byte edge
- superseded by [[decision-o1-add-edge]] - which exploits this format for the O(1) write path
- causes [[eight-relation-types-cap]] - the residual boundary it left open
