---
id: type-registry
title: Compile-time type registry
type: component
tags: [types, compile-time]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Compile-time type registry
> node_type_of<T> maps a C++ payload type to its on-disk NodeType tag.

## Responsibility

Map a C++ payload type to its on-disk `NodeType` tag at compile time, and report a type's
on-disk payload size.

## Where it lives

`graph_core/struct/type_registry.h` and `graph_core/struct/type_registry.cpp`.

## How it behaves

- The primary template `node_type_of<T>` is intentionally **left undefined**, so an
  unsupported payload type fails to compile with a clear error rather than silently picking
  a tag.
- Specializations: `int`->INT, `float`->FLOAT, `double`->DOUBLE, `char`->CHAR, `bool`->BOOL,
  and `ComplexRecord`->COMPLEX.
- `node_type_of_v<T>` is the value alias used by `insert` and `write_node` for `if constexpr`
  dispatch.
- `node_record_payload_size(NodeType)` returns the on-disk payload size. For COMPLEX it
  returns only `sizeof(ComplexHeader)` - the two variable-length strings are not counted, so
  the delete path reads the real size from the header instead.

## Contracts and constraints

- Adding a payload type means three coordinated edits: a new `NodeType` enumerator, a
  `node_type_of` specialization, and a new `case` in the `read_node` switch.

## Links

- part of [[pod-layout]] - the tag it maps to is a POD field
- used by [[persistence-io]] - drives write and read dispatch
- relates to [[complex-nodes]] - COMPLEX is the one non-trivially-copyable payload
