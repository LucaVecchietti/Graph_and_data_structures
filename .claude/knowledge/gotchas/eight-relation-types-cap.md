---
id: eight-relation-types-cap
title: Hard cap of 8 relation types per node
type: gotcha
tags: [disk-format, trap]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Hard cap of 8 relation types per node
> A node with more than 8 relation types throws, because relation batch chaining is not implemented.

## Symptom

`std::runtime_error` from `write_relation_node_list`, `persist_new_edge` or
`update_node_edges` when a node acquires its ninth distinct relation type. Fewer than nine
relation types per node, at any fanout, is fine.

## Root cause

A [[relation-batch]] has a fixed tail of `RELATION_LINES_PER_BATCH = 8` lines. The format
reserves `next_offset` and `head` in the header for chaining a second batch, but **the
chaining is not implemented** - so a full batch has nowhere to put the ninth line and the
writers throw instead of corrupting the format.

Note this is a cap on distinct relation **types** per node, not on edges: a single relation
can hold an unbounded chain of edges.

## Fix

There is no workaround inside the current format other than modelling fewer relation types per
node. Lifting the cap means implementing batch chaining (allocate a second batch, link it via
`next_offset`, advance `head` 1 -> 2 -> 3, and teach the read path and `delete_node_from_disk`
to walk the chain). It is the first open item on the roadmap.

## How to avoid it

Before adding a relation type to a hot node, count the existing ones. Any new writer that
touches relation lines must keep throwing on a full batch rather than silently overrunning the
2176-byte tail.

## Links

- caused by [[relation-batch]] - the fixed 8-line tail
- caused by [[decision-fixed-width-relation-batch]] - the residual boundary that decision left open
- relates to [[decision-o1-add-edge]] - `persist_new_edge` throws on a full batch
