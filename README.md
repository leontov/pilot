# Kolibri Ω

Kolibri Ω is a numerical intelligence node built entirely around decimal programs and a compact execution runtime. This repository contains the first implementation stage (A) that provides the foundational components required to boot the node, execute decimal bytecode, and expose a minimal web/HTTP interface.

## Project layout

```
/
  README.md
  LICENSE
  Makefile
  kolibri.sh
  cfg/
  bin/
  data/
  logs/
  include/
  src/
  tests/
  web/
  .github/
```

Key subsystems delivered in this milestone:

* **Δ-VM v2** – A decimal stack virtual machine with the initial opcode set (PUSHd–RET) and deterministic execution limits.
* **Fractal KV (F-KV)** – An in-memory decimal trie with prefix lookup. Persistence hooks and compression points are stubbed for later milestones.
* **HTTP + CLI** – Minimal HTTP server exposing `/status`, `/run`, and `/dialog`. The CLI script `./kolibri up` builds the project, prepares the web assets, and boots the node.
* **Web Studio** – Lightweight Vite + TypeScript SPA that connects to the HTTP API and renders console-style panels for dialog, VM trace, and memory previews.

## Building and running

Prerequisites: GCC (C11), Make, Node.js 18+, and npm.

```
./kolibri.sh up
```

The command builds the native node, installs web dependencies, produces the static bundle, and launches the backend on `http://localhost:9000`. Logs are written to `logs/kolibri.log`.

Other CLI commands:

```
./kolibri.sh stop   # stop running node
./kolibri.sh bench  # run built-in benchmarks (placeholder)
./kolibri.sh clean  # remove build artifacts, logs, and data
```

## Testing

```
make test
```

Unit tests cover the Δ-VM arithmetic instructions and the F-KV prefix lookup.

## Roadmap

Stage A focuses on the core loop and interfaces. Later milestones will extend the F-KV with persistence/compression, add program synthesis strategies, incorporate the knowledge blockchain, and expand the HTTP/UI surfaces.
