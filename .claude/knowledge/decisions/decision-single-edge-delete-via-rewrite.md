---
id: decision-single-edge-delete-via-rewrite
title: Single-edge delete on top of the whole-node rewrite
type: decision
tags: [edges, delete]
aliases: [delete_edge]
created: 2026-08-10
updated: 2026-08-10
status: active
---

# Single-edge delete on top of the whole-node rewrite
> delete_edge reuses update_node_edges and resolves an edge id by scanning, trading O(1) for one shared delete path.

## Context

As long as only `delete_node` existed, the only way to remove an edge was to delete one of its
endpoints. Two entry points were wanted: by `(start, end, relation)` triple, and by the global
`Edge.id`.

## Options considered

- **Build both on the existing whole-node rewrite** - chosen for now.
- **Write the O(1) unlink immediately** (patch the neighbours' `prev_offset`/`next_offset`, free
  the 48-byte slot, decrement `edge_count` on that one relation line in place) - the right shape,
  but independent and riskier work; deferred so the API could land correct and covered first.
- **A standalone body for the id overload** - would duplicate the delete logic, giving two places
  to get the reverse index or the counters wrong.
- **A persistent `edge_id` -> offset index** to avoid the scan - a new file to keep consistent
  through every splice; disproportionate to current use.

## Decision

Both overloads erase the `EdgeRef` from the RAM adjacency and re-persist the node with
`update_node_edges`. The id overload does **not** duplicate anything: it resolves
`edge_id -> (start, end, relation)` and delegates to the triple overload, so exactly one delete
path exists (rewrite + reverse index + counters).

Resolution checks the resident nodes first, then falls back to `find_edge_by_id`, which walks the
live-node topology the way `build_inbound_index` does. A flat scan of `edges.dat` would **not**
be sound: a freed 48-byte slot is zeroed and reads back as `id = 0`, indistinguishable from the
real edge id 0.

## Consequences

- Cost is **O(total degree of the node)**, not O(1): the rewrite re-lays every relation of the
  node. And `edges.dat` grows by `48 * surviving edges` per call, because the runs are re-appended
  at EOF while the freed slots wait on the `edges` bin for the next `add_edge`.
- The id overload pays O(N+E) whenever the owning node is not resident.
- It surfaced [[reverse-index-node-granularity]]: `delete_edge` is the first path that removes
  *one* relation, which is what made the node-to-node granularity of [[in-edges-index]] matter.
- Reading trap in the public surface: `delete_edge(5)` deletes the edge with **id** 5,
  `delete_edge(5, 3)` deletes the edge 5->3 - same name, arity is the only difference.
- No coverage in [[smoke-test]]: both overloads were verified with a throwaway harness only.

## Links

- specifies [[graph-class]] - both overloads are public methods of Graph
- depends on [[persistence-io]] - reuses `update_node_edges` and adds `find_edge_by_id`
- causes [[reverse-index-node-granularity]] - the trap this path exposed
- relates to [[decision-o1-add-edge]] - the O(1) counterpart on the write side, still missing here
