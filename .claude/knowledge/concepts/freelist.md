---
id: freelist
title: Size-segregated freelists
type: concept
tags: [freelist, disk]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Size-segregated freelists
> Freed disk regions are recycled through LIFO bins segregated by exact size, so every reuse is an exact fit.

## What it is

Freed disk regions are recycled through LIFO bins that are segregated by **exact** size, so
every reuse is an exact fit: no scanning, no best-fit, no splitting, no wasted bytes.

## How it works

- One bin file per distinct free-region size: `db/freelist/<prefix>_<size>.dat`. The size is
  encoded in the filename, which is what makes the fit exact by construction.
- `push` (on delete or orphan) appends one record - O(1). `pop` (on insert or reuse) reads
  the last record and shrinks the file by `resize_file` - O(1), LIFO.
- Prefixes:
  - `nodes` - `NodeFreeOffset`, sizes in {1, 4, 8} for primitives, and the record also carries
    the reusable `idx` so a node id is recycled along with its region.
  - `rel` - one single size class, because every [[relation-batch]] is 2213 bytes.
  - `edges` - one single size class of 48 bytes, one `Edge` slot at a time.
  - `complex` - one class per `type_label` length, see [[complex-nodes]].
- `db/freelist/json_prog.dat` is a flat LIFO stack of recycled COMPLEX `prog_number`s: a
  sidecar file has no offset, so reclaiming it means deleting the file and recycling its number.
- Writers `insert` and `add_edge` pop; `delete_node` and `update_node_edges` push.

## Why it matters here

The push-then-pop ordering inside `update_node_edges` is what makes an edge weight overwrite
cost **zero file growth**: step 2 frees regions whose sizes are byte-identical to the ones
step 3 immediately needs, so the LIFO bins hand back exactly those regions.

## Links

- part of [[persistence-io]] - the bin operations live in this layer
- depends on [[pod-layout]] - the three free-offset records are PODs
- specified by [[decision-exact-size-freelist-bins]] - why exact-size bins over best-fit
- relates to [[tombstoning]] - node id slots are recycled through the `nodes` bin
- relates to [[complex-nodes]] - the `complex` bins act as per-type size classes
