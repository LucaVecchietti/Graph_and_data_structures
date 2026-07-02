---
node: relation-batch
title: Fixed-Width Relation Batch
type: component
tags: [relations, disk-format, o1]
updated: 2026-07-01
---

# Fixed-Width Relation Batch

`NodeRelationList` stores a node's relations as one or more equal-size batches — the design that makes O(1) `add_edge` and a single-size-class relation freelist possible.

## Facts

- One batch = a 37-byte `NodeRelationList` POD header + a fixed `RELATION_BATCH_TAIL` = 2176-byte tail → always **2213 bytes**, regardless of lines used (`pod_struct.h:81-127`).
- Tail holds up to `RELATION_LINES_PER_BATCH = 8` lines, each `RELATION_LINE_SIZE = 272` bytes: `[edge_offset(8)][edge_count(8)][name_len(1)][name[255]]` (`costants.h:16-19`).
- Line `i` lives at `tail + i * 272`, so one relation's `edge_offset`/`edge_count` is updated with a **direct seek-write** (O(1)) — the foundation of O(1) `add_edge`.
- Constant batch size → the `rel` [[freelist]] needs only ONE size class.
- Header fields: `node_id`, `type_count` (0..8 used), `batch_size` (2176), `free_bytes`, `next_offset` (chain to next batch), `head` (serial 1,2,3…), `is_deleted`.
- **>8 relation types** would chain a 2nd batch via `next_offset` — **batch chaining is WIP**; `write_relation_node_list` throws if `type_count > 8` (`graph_io.h:212-215`).

## Relations

- **part-of** → [[pod-layout]]
- **relates-to** → [[edge-record]]
- **documented-in** → docs/modules/db.md

## Sources

- `graph_core/struct/pod_struct.h:81-127`, `graph_core/costants.h:7-19`, `graph_core/io/graph_io.h`
