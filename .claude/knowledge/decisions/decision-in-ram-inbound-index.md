---
id: decision-in-ram-inbound-index
title: Keep the inbound edge index in RAM only
type: decision
tags: [ram, delete]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Keep the inbound edge index in RAM only
> Rebuild the reverse edge index at load instead of persisting it, trading an O(N+E) scan for schema stability.

## Context

The model is directed and stores adjacency only on the source side
(`BaseNode::neighborgs[relation][to_id]`). Deleting a node X must also remove **inbound**
edges, or other nodes keep neighbours that point at a tombstoned slot after a reload. But
"who points at X?" is not answerable: there is no index by `to_node`. The `Edge` POD does
carry `from_node`, so an edge identifies its owner - the problem is finding those edges
without scanning `edges.dat` on every delete.

## Options considered

- **Scan `edges.dat` on every delete** - minimal to implement, but O(E) per deletion, and it
  has to cope with zeroed regions that look like edges.
- **Persist the reverse adjacency on disk** - delete becomes O(deg_in), but it doubles the
  writes and touches the fragile on-disk format.
- **Rebuild a RAM-only index at load** - chosen.

## Decision

`Graph::in_edges` is an `unordered_map<int, unordered_set<int>>`, never persisted.
`build_inbound_index(next_id)` builds it with an O(N+E) scan that follows only live nodes,
skipping tombstones so zeroed chunks are never read as edges. It is then maintained
incrementally by `add_edge` and `delete_node`.

## Consequences

- The `Graph` constructor now pays an O(N+E) scan at load, where before it only read `meta`.
  Acceptable for a prototype on small graphs.
- No schema change and no extra writes - the structure is volatile by design.
- `delete_node` reloads and re-persists the inbound owners, so no dangling neighbour survives
  a reload; `edge_count` is now genuinely decremented on delete.

## Links

- specifies [[in-edges-index]] - the mechanism this decision defines
- relates to [[tombstoning]] - the load scan must skip tombstoned slots
- relates to [[graph-class]] - Graph owns and maintains the index
