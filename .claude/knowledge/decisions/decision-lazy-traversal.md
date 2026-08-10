---
id: decision-lazy-traversal
title: Lazy load on every frontier pop
type: decision
tags: [traversal, ram]
aliases: [ensure_loaded]
created: 2026-08-10
updated: 2026-08-10
status: active
---

# Lazy load on every frontier pop
> Materialise each node as it leaves the traversal frontier, through one shared ensure_loaded that throws instead of skipping.

## Context

`Graph::traverse` only ever looked at the in-RAM `nodes` map: a neighbour discovered mid-walk
was dropped with a `continue`, so on a cold store a walk stopped at **depth 1** and reported no
error. The gap was invisible because every phase of [[smoke-test]] forced the loads by hand with
a throwaway `add_edge(x, y, "_load")` call - a trick that also wrote fake edges into the store.
Meanwhile the same lazy-load block (resident? id ever assigned? readable?) was copy-pasted in
`add_edge`, `delete_node` and `delete_edge`.

## Options considered

- **Load on every frontier pop, through one shared helper** - chosen. Depth-independent by
  construction.
- **Preload every node when the `Graph` is constructed** - trivial, but defeats lazy loading and
  costs an O(N+E) read on every open.
- **Load only the start node's neighbours** (the first attempt, commit `175a877`) - covers only
  depth 1, which already worked.
- **Keep skipping unreadable nodes silently** - tolerant of leftovers, but hides an inconsistent
  store and returns traversal results that are quietly wrong.

## Decision

The load happens when a node is **popped** from the frontier, via a single private member
`Graph::ensure_loaded(int id, const std::string &action)` shared by `add_edge`, `delete_node`,
`delete_edge` and `traverse`. `action` only shapes the log line, so per-caller log context
survived the de-duplication (it replaced six copies of the block).

Failure is **fatal**: `std::out_of_range` for an id that was never assigned,
`std::runtime_error` for a record that cannot be read - typically a tombstoned slot reached by a
dangling edge. Fail-fast became defensible once [[reverse-index-node-granularity]] closed the
main source of dangling edges.

The one deliberate exception: `delete_node`'s inbound cleanup wraps the call in `try/catch` and
**skips** an unreadable owner, because a delete must still reclaim the node it was asked to
remove.

## Consequences

- Changes the contract of `traverse`/`bfs`/`dfs`: they never threw before, now they do.
- A walk materialises the **whole reachable component**, and `nodes` has no eviction - RAM grows
  monotonically for the lifetime of the `Graph`. Acceptable for a single-thread embedded engine;
  the signal to revisit is any need for a bounded working set.
- The "node not found" branch inside `traverse` became unreachable and was removed:
  `ensure_loaded` either inserts or throws.
- [[smoke-test]] could drop the `"_load"` trick and become self-checking.

## Links

- specifies [[traversal-policies]] - the template where the per-pop load lives
- specifies [[graph-class]] - `ensure_loaded` is a member of Graph, used by every entry point
- relates to [[reverse-index-node-granularity]] - fail-fast is only safe once dangling edges are gone
- verified by [[smoke-test]] - phase 2 walks a 2-hop chain from a cold store
