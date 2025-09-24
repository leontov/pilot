<!-- Copyright (c) 2024 Кочуров Владислав Евгеньевич -->

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
* **HTTP + CLI** – Minimal HTTP server exposing `/api/v1/health`, `/api/v1/dialog`, `/api/v1/vm/run`, `/api/v1/fkv/get`, `/api/v1/program/submit`, and `/api/v1/chain/submit`. The CLI script `./kolibri up` builds the project, prepares the web assets, and boots the node.
* **HTTP + CLI** – Minimal HTTP server exposing `/api/v1/health`, `/api/v1/metrics`, `/api/v1/vm/run`, `/api/v1/dialog`, and `/api/v1/fkv/get`. The CLI script `./kolibri up` builds the project, prepares the web assets, and boots the node.
* **Web Studio** – Lightweight Vite + TypeScript SPA that connects to the HTTP API and renders console-style panels for dialog, VM trace, and memory previews.
* **Runtime entrypoint** – `src/main.c` is the only native launcher; it wires VM execution, F-KV storage, Kolibri AI orchestration, and HTTP routing. The legacy UDP prototype from `src/kolibri_node_v1.c` has been removed to keep the build aligned with the current subsystems.【F:src/main.c†L1-L118】【F:CMakeLists.txt†L24-L41】

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
./kolibri.sh bench  # run Δ-VM & F-KV microbenchmarks (mean/p95/stddev latency, VM steps)
./kolibri.sh clean  # remove build artifacts, logs, and data
```

The suite runs three deterministic Δ-VM programs plus 1000 `fkv_put`/`fkv_get` prefix lookups. Each line reports mean, p95,
standard deviation, minimum, and maximum latency in microseconds alongside the achieved throughput (ops/s); Δ-VM entries also
print average/min/max step counts and the HALT ratio to confirm execution limits while F-KV validates stored values. Results
are appended to `logs/bench.log` and exported as structured JSON in `logs/bench.json` for programmatic comparison.

### Console chat mode

For quick experiments without the web UI, build the node and launch the interactive chat harness:

```
make build
./bin/kolibri_node --chat
```

The CLI accepts decimal arithmetic expressions (e.g. `12+30/6`) and stores the results in the F-KV memory. Any other text prompt
falls back to the current best formula discovered by the Kolibri AI core, which is useful for smoke-testing the synthesis loop.

### HTTP API endpoints

Kolibri Ω now exposes a richer JSON API that mirrors the CLI behaviour:

* `GET /api/v1/health` – returns uptime information and whether a blockchain peer is attached.
* `GET /api/v1/metrics` – surfaces the latest Kolibri AI planning metrics alongside request counters.
* `GET /status` (port `http.port + 1000`) – lightweight status endpoint exposing node uptime, the current F-KV root top-K view, and an inline snapshot of the Kolibri AI planner metrics for external dashboards.
* `POST /api/v1/dialog` – accepts `{ "input": "7+8" }`, executes the expression on the Δ-VM, stores the result in F-KV, nudges the AI curriculum, and responds with `{ "answer": "15", "status": "vm", "steps": 6, "stored": true }`. If the expression cannot be evaluated the handler falls back to the strongest known formula.
* `POST /api/v1/vm/run` – executes raw decimal bytecode supplied via the `program` array and returns the VM status, result, and halt flag. Optional `max_steps`/`max_stack` keys override the config limits per request.
* `GET /api/v1/fkv/get?prefix=...&limit=...` – performs a prefix lookup in the fractal memory and returns the top matches (keys and decimal values) for observability tooling.

## AI state persistence

The Kolibri AI subsystem snapshots its working memory (`KolibriMemoryModule`), training dataset, and the most recent scoring
metrics to a structured JSON file whenever the node is stopped or destroyed. Snapshots are automatically reloaded on startup so
the node can resume from the last session. The location and retention policy live under the `ai` section of
[`cfg/kolibri.jsonc`](cfg/kolibri.jsonc):

```jsonc
  "ai": {
    "snapshot_path": "data/kolibri_ai_snapshot.json", // JSON file with memory + dataset
    "snapshot_limit": 2048                              // maximum entries persisted (0 = unlimited)
  }
```

The snapshot file is created on demand (directories are created automatically). Setting `snapshot_limit` to zero disables
trimming and keeps the entire history.

Each vertex of the F-KV trie keeps only the top-K most recent entries for its prefix. The default capacity can be tweaked via
the optional `fkv` block in [`cfg/kolibri.jsonc`](cfg/kolibri.jsonc):

```jsonc
  "fkv": {
    "top_k": 4
  }
```

Requests to `/api/v1/fkv/get` or the CLI will surface entries in recency order based on this limit.

## Testing

```
make test
```

Unit tests cover the Δ-VM arithmetic instructions, the F-KV prefix lookup, and the HTTP routing layer (`make test-http-routes`).

## Roadmap

The end-to-end development plan is captured in [`docs/roadmap.md`](docs/roadmap.md). It outlines milestone gates (Prototype Core → Networked Intelligence → Product Readiness → Pilot Launch), sprint-by-sprint deliverables, cross-cutting workstreams, and risk mitigations that keep Kolibri Ω aligned with its ≤ 50 MB decimal-first mandate.

## Demo playbook

For investor-grade demonstrations and product validation scenarios, consult [`docs/demos.md`](docs/demos.md). The playbook now outlines seventy-eight curated demos, associated KPIs, and exact launch instructions for Kolibri Studio and the public HTTP API.

## Documentation bundle

* [README Pro](docs/readme_pro.md) – investor-facing overview of product pillars, KPIs, and launch instructions.
* [Whitepaper](docs/whitepaper.md) – full architecture and research narrative for the decimal-first intelligence stack.
* [HTTP API Specification](docs/api_spec.md) – endpoint catalogue, payload schemas, and operational policies for `/api/v1`.
* [Technical overview](docs/architecture.md) – subsystem walkthrough linking source code and configuration surfaces.
