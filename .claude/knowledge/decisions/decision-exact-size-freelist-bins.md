---
id: decision-exact-size-freelist-bins
title: Exact-size segregated freelist bins
type: decision
tags: [freelist, disk]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Exact-size segregated freelist bins
> One bin file per distinct free-region size, so push and pop are O(1) and every reuse is an exact fit.

## Context

Every `add_edge` orphaned the old relation list and edge chunks, and the database grew
monotonically. The original placeholder - a `FreeRecord` holding a single `uint64_t offset`,
plus `MetaRecord.free_count` - was unusable: an offset without the **size** of the region
cannot be reused safely.

## Options considered

- **A single flat freelist with first-fit** - without a size, every reuse would need a scan
  and would risk partial fits and internal fragmentation.
- **A single freelist of `(offset, size)` with best-fit** - needs sorting or an O(n) scan.
- **Mark-and-sweep / periodic compaction** - deferred; it means rewriting and re-indexing
  whole files, and gives nothing on the common case (an edge overwrite), where popping the
  just-freed hole avoids all growth.

## Decision

A persistent freelist of bins **segregated by exact size**: one file per distinct free-region
size under `db/freelist/<prefix>_<size>.dat`, with the size encoded in the filename. Push is
an append (O(1)); pop reads the last record and truncates the file by one record (O(1), LIFO).
`FreeRecord` is replaced by three PODs: `NodeFreeOffset {idx, offset, size}` (carrying the
reusable id), `RelationNodeListFreeOffset {offset, size}` and
`BatchOfEdgesFreeOffset {idx, offset, size}`.

## Consequences

- Every pop is an exact fit by construction: no scan, no file rewrite, no wasted bytes.
- Many small files, one per size class - accepted deliberately.
- LIFO gives temporal locality but no compaction guarantee.
- Later work extended reuse from the `nodes`/`complex` bins on `insert` to the `rel`/`edges`
  bins inside `update_node_edges`, which is what makes a weight overwrite cost zero growth.

## Links

- specifies [[freelist]] - the mechanism this decision defines
- relates to [[tombstoning]] - node ids are recycled through the `nodes` bins
- relates to [[decision-o1-add-edge]] - single-slot `edges` bins come from that work
