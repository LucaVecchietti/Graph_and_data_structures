---
id: logger
title: Logger
type: component
tags: [logging]
aliases: []
created: 2026-08-09
updated: 2026-08-09
status: active
---

# Logger
> Header-only leveled logger writing to both a log file and stderr.

## Responsibility

Leveled diagnostic logging for the engine, with no external dependency.

## Where it lives

`graph_core/logger.h` - header-only. Output lands in `graph.log` and `graph_io.log` at the
process working directory.

## How it behaves

- `enum class LogLevel { DEBUG, INFO, WARN, ERR }`; entries below `min_level` are dropped.
- Each entry is `timestamp [LEVEL] msg` and is written to **both** the log file and
  `std::cerr`, flushed per line.
- Timestamps are local time, `%Y-%m-%d %H:%M:%S`.
- `Graph` constructs `Logger("graph.log", LogLevel::DEBUG)`; the I/O translation unit logs to
  `graph_io.log`.

## Contracts and constraints

- No mutex and no locking - it is not safe for concurrent use, in line with the rest of the
  single-threaded engine.
- Per-line flush to two sinks makes it slow enough to matter in a hot loop.

## Links

- used by [[graph-class]] - owns a Logger writing to graph.log
- used by [[persistence-io]] - logs to graph_io.log
