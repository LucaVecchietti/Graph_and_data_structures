---
id: decision-o1-add-edge
title: add_edge in O(1) - append, relink, patch one line
type: decision
tags: [perf, edges]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# add_edge in O(1) - append, relink, patch one line
> Exploit the fixed-width batch to add an edge without rewriting the node, and overwrite a weight with an 8-byte in-place write.

## Context

[[decision-fixed-width-relation-batch]] froze a format that *could* support constant-time edge
insertion, but left `add_edge` still going through `update_node_edges`, a whole-node rewrite
costing O(deg) per edge. This decision cashes in the format.

## Options considered

- **Keep the whole-node rewrite** - simple, but O(deg) per edge, which was the whole problem.
- **Do not store the edge's disk offset in RAM, and find it by walking the chain on an
  overwrite** - O(deg) per overwrite; storing the offset makes it O(1) for 8 bytes per edge in
  RAM, at the cost of an invariant to maintain.
- **Recycle edge ids from the popped freelist record** - rejected; every edge keeps its own
  globally stable id, and the popped record's `idx` is ignored.

## Decision

- **New edge -> `persist_new_edge`**: find the relation's line (at most 8 lines, so O(1)),
  allocate the `Edge` slot by popping the `edges` bin or appending, write it with `prev = 0`
  and `next =` current chain head, patch the old head's `prev_offset`, then update **only**
  that relation line in place (new head offset, `edge_count + 1`) - or write a fresh line at
  slot `type_count` for a brand new relation type. The batch never moves, so `nodes.idx` is
  never touched.
- **Weight overwrite -> `persist_edge_weight`**: seek to `edge_offset + offsetof(Edge, weight)`
  and write 8 bytes. No allocation, no relink, no growth.
- `EdgeRef` gains an `offset` field, set at load, at append, and refreshed by
  `update_node_edges` whenever it relocates edges.
- `update_node_edges` survives only for the inbound cleanup inside `delete_node`.

## Consequences

- `nodes.dat` does not grow when an edge is added (the line is updated inside the already
  allocated batch); `edges.dat` grows by 48 bytes for a genuinely new edge, or reuses a freed
  slot, and by zero on an overwrite.
- `free_edge_count` now counts individual 48-byte edge slots, not chunks.
- **Invariant to respect:** `EdgeRef.offset` in RAM must stay aligned with disk. It holds only
  as long as `update_node_edges` remains the sole code path that relocates edges.
- Residual boundary at the time: a full batch threw. Lifted on 2026-08-09 by
  [[decision-relation-batch-chaining]], which allocates and links a further batch - at the
  cost of making the line lookup O(batches) rather than strictly O(1).

## Links

- supersedes [[decision-fixed-width-relation-batch]] - completes the work that decision deferred
- implemented by [[graph-class]] - `add_edge` dispatches between the two paths
- implemented by [[persistence-io]] - `persist_new_edge` and `persist_edge_weight`
- depends on [[edge-record]] - the O(1) splice into the chain
- relates to [[decision-relation-batch-chaining]] - what happens when the batch it writes into is full
