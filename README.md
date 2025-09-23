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
* **HTTP + CLI** – Minimal HTTP server exposing `/api/v1/health`, `/api/v1/vm/run`, `/api/v1/dialog`, and `/api/v1/fkv/get`. The CLI script `./kolibri up` builds the project, prepares the web assets, and boots the node.
* **Web Studio** – Lightweight Vite + TypeScript SPA that connects to the HTTP API and renders console-style panels for dialog, VM trace, and memory previews.

## Building and running

Prerequisites: GCC (C11), Make, Node.js 18+, and npm.

```
./kolibri.sh up
```

The command builds the native node, installs web dependencies, produces the static bundle, and launches the backend on `http://localhost:9000`. Logs are written to `logs/kolibri.log`.

> **Note:** The web client expects the backend base URL to be provided through the `VITE_API_BASE` environment variable during the Vite build. For local development this is typically `http://localhost:9000`, e.g. `VITE_API_BASE=http://localhost:9000 npm run build`.

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

The end-to-end development plan is captured in [`docs/roadmap.md`](docs/roadmap.md). It outlines milestone gates (Prototype Core → Networked Intelligence → Product Readiness → Pilot Launch), sprint-by-sprint deliverables, cross-cutting workstreams, and risk mitigations that keep Kolibri Ω aligned with its ≤ 50 MB decimal-first mandate.

## Demo playbook

For investor-grade demonstrations and product validation scenarios, consult [`docs/demos.md`](docs/demos.md). The playbook now outlines seventy-eight curated demos, associated KPIs, and exact launch instructions for Kolibri Studio and the public HTTP API.
