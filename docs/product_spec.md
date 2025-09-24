<!-- Copyright (c) 2024 Кочуров Владислав Евгеньевич -->

# Kolibri Ω — Product Mission & Execution Blueprint

## 1. Product Mission & Success Criteria
- **Mission Statement:** Deliver a deterministic decimal intelligence node that autonomously synthesises, stores, and exchanges knowledge without neural weights, packaged in a ≤ 50 MB core and reproducible across commodity x86-64/ARM hardware.
- **North-Star KPI:** Investor-grade demo readiness by T+12 weeks, measured by full execution of 30 scripted scenarios with 0 critical defects.
- **Guardrail KPIs:**
  - Core binary size ≤ 50 MB, cold start ≤ 300 ms.
  - P95 request latency (VM + API) < 50 ms at 500 RPS.
  - VM gas accounting correctness: zero tolerance for runaway execution in unit/property suites.
  - F-KV throughput: ≥ 1 M keys loaded, prefix GET P95 < 10 ms.
  - Auto-synthesis overnight PoE uplift ≥ 20 %; ≥ 90 % promoted programs revalidated successfully.
  - Network replication: new knowledge blocks reach ≥ 3 peers in < 3 s, ≥ 95 % uptime over 24 h soak.
  - QA exit: 24 h ASAN/UBSAN clean, fuzzers find 0 new critical issues in last 72 h, documentation (README Pro, API Spec, Demo Guide) approved.

### KPI Traceability Matrix
| Objective | Metric | Target | Measurement Cadence | Owner |
|-----------|--------|--------|---------------------|-------|
| Compact core | Binary footprint | ≤ 50 MB | Nightly CI packaging | Core Team |
| Fast response | P95 `/api/v1/vm/run` latency | < 50 ms @ 500 RPS | Load harness (daily) | Platform |
| Determinism | Trace hash variance | 0 under identical seeds | Regression tests (per commit) | QA |
| Knowledge quality | PoE uplift overnight | ≥ 20 % | 08:00 UTC synthesis report | Synthesis |
| Memory scale | Prefix GET latency | < 10 ms P95 @ 1 M keys | Weekly F-KV bench | Storage |
| Network health | Block propagation | < 3 s to 3 peers | Cluster soak (daily) | Network |
| Demo readiness | Script completion rate | 30/30 scenarios pass | Pre-demo rehearsal (weekly) | Product |

## 2. Δ-VM Core Specification
- **Architecture:** Stack-based deterministic interpreter with decimal opcode set `{PUSHd, ADD10, SUB10, MUL10, DIV10, CMP, JZ, JNZ, CALL, RET, READ_FKV, WRITE_FKV, HASH10, RANDOM10, TIME10, HALT}`. Threaded dispatch with pre-decoded opcode table keeps branch mispredictions under control. Optional JIT plugin (guarded by feature flag) emits straight-line decimal ALU kernels for hot traces.
- **Execution Limits:**
  - Gas limit enforced per program (default 10 000 steps) with configurable policy, surfaced as `/cfg/vm/gas_limit` and overridable per HTTP request for testing.
  - Memory limits: stack depth cap 1 024 frames, frame size 256 slots; heap arena 2 MB default with bump allocator per program.
  - Deterministic entropy: `RANDOM10` seeds derived from scenario ID; replay harness stores `{seed, gas_limit}` alongside trace hash.
- **Performance Targets:** P95 runtime for 256-step program < 50 ms on 3.2 GHz x86-64 and < 70 ms on Apple M1. JIT module must yield ≥ 2× speedup on arithmetic microbenchmarks and degrade gracefully (disabled) on unsupported platforms.
- **Traceability:**
  - JSON trace per instruction: `{pc, opcode, operands, stack_top, stack_depth, gas_remaining, fkv_reads, fkv_writes, timestamp}` with optional binary protobuf mirror for compact archival.
  - CLI `kolibri.sh trace <program>` dumps trace to `logs/trace.jsonl`; `/api/v1/vm/run` responds with `trace_url` pointing to the artifact.
  - Step debugger: `kolibri.sh vm step --program <file> --breakpoint <pc>` for manual QA.
- **Testing:**
  - Unit coverage for each opcode (happy path + edge cases) ≥ 2 test cases/opcode plus error-path assertions (divide-by-zero, gas exhaustion, stack overflow).
  - Property tests: decimal arithmetic commutativity/associativity (where applicable), gas monotonicity, determinism under identical seeds, serialization ↔ trace replay equivalence.
  - Golden programs: arithmetic suite, factorial, Fibonacci, memory IO, control flow, and failure injection stored in `tests/vm/programs/*.dvm`.
  - Bench harness: `tests/vm/bench_*.c` executed nightly with threshold alarms and exported to Prometheus.

## 3. Fractal KV (F-KV) Specification
- **Structure:** 10-ary trie with node buckets storing top-K (default 8) items sorted by PoE score. Nodes carry aggregated metrics (PoE mean, MDL delta, gas usage) and cross-links for sibling prefetching.
- **APIs:**
  - `fkv_put(key, value, type, metadata)` — writes decimal-encoded value, updates PoE/MDL aggregates, returns `{version, gas_used}`.
  - `fkv_put_scored(key, value, type, priority)` — injects explicit PoE/MDL composite score to steer top-K ranking per prefix.
  - `fkv_get_prefix(prefix, limit, type_filter)` — returns ordered list with latency target P95 < 10 ms and streaming pagination for long tails.
  - `fkv_topk_programs(prefix, k)` — fetches highest PoE procedural memories with optional freshness filter.
  - Admin endpoints: `fkv_compact`, `fkv_snapshot`, `fkv_stats` for observability and control.
- **Persistence & Compression:**
  - Write-ahead log + periodic snapshot; arithmetic coding for node payloads targeting 30 %+ compression. Snapshots versioned and checksummed (`SHA256`) for deterministic replay.
  - GC cadence: reclaim stale entries nightly, ensure disk usage growth < 5 % week-over-week. Cold tiers moved to compressed object store (`/data/fkv/archive`).
  - Memory budget: in-memory trie cache ≤ 8 GB, eviction policy LRU with PoE weighting.
- **Testing & Tooling:**
  - Synthetic load generator `kolibri.sh fkv-bench` populates 1 M keys, reports throughput/latency, and exports histograms.
  - Consistency checks comparing trie traversal vs serialized form; fuzzers mutate decimal paths ensuring no panic.
  - Integration tests verifying Δ-VM roundtrips (WRITE_FKV → READ_FKV) under varying gas costs.

## 4. Δ-VM ⇄ F-KV Integration
- **Instruction Semantics:**
  - `READ_FKV`: pops prefix & type, pushes JSON handle referencing retrieved values. Gas fee = base (5) + per-result (1) + trie depth factor.
  - `WRITE_FKV`: pops key/value/type, returns status code and gas cost proportional to serialized length with surcharge when compaction backlog > threshold.
  - `HASH10` over payloads ensures deterministic deduplication before commit.
- **Memory Profiles:** Templates for episodic, semantic, procedural stores with default gas accounting and PoE update rules. Profiles defined in `/cfg/memory_profiles.json` and referenced by VM metadata.
- **Scenarios:** Regression programs covering fact write/readback, procedural program promotion, conflict resolution, concurrent writers (optimistic locking), and eviction paths.
- **Observability:** Trace entries link to F-KV operations (`fkv_op_id`) plus WAL sequence numbers. `/metrics` exports per-op counters and latency histograms.

## 5. HTTP API v1 & CLI
- **Routes:**
  - `GET /status` → node health, build hash, uptime, memory, peers, current KPI snapshot.
  - `GET /metrics` → Prometheus exposition with VM/F-KV/Network counters.
  - `POST /run` → execute Δ-VM program; response includes `result`, `trace_url`, `gas_used`, `gas_limit`, `warnings[]`.
  - `POST /dialog` → conversation turn using VM-backed orchestrations with optional `context_id` for episodic threading.
  - `GET /fkv/prefix` → wrapper for `fkv_get_prefix` with pagination cursors and `Accept: application/x-ndjson` streaming option.
  - `POST /program/submit` → ingest candidate program with PoE/MDL metrics, returns `evaluation_id` for asynchronous scoreboard updates.
  - `POST /chain/submit` → broadcast knowledge block, returns attestation summary and gossip fanout.
  - `POST /orchestrator/run` → trigger synthesis job (enumerative, MCTS, genetic) with resource budget.
- **Non-functional Targets:** All routes P95 < 75 ms (including network overhead); JSON schema validation; authentication stub via API keys with per-route rate limits and audit logging. Error responses include machine-readable codes.
- **CLI `kolibri.sh`:**
  - Commands: `up`, `down`, `status`, `trace`, `bench`, `demo` (runs scripted investor demo), `fkv-bench`, `chain-sim`, `synth-run`.
  - Idempotent operations, exit codes documented, logs rotated (< 50 MB per service). Shell completion script published for bash/zsh.

## 6. Kolibri Studio (Vite/React SPA)
- **Tabs & Views:**
  - **Dialog:** chat interface, streaming responses, trace panel linking to VM steps, gas gauge, and replay controls.
  - **Memory:** F-KV explorer with prefix search, PoE/MDL visualisations, writeback actions, and diff view between snapshots.
  - **Programs:** bytecode editor with syntax hints, upload/download, integrated trace inspector.
  - **Synthesis:** leaderboard of candidate programs, PoE/MDL charts, control panel to launch runs.
  - **Blockchain & Cluster:** block explorer, peer reputation heatmap, gossip stream monitor.
  - **Observability:** metrics dashboards, alert feed, log tailing.
- **Data Flow:** React Query fetching from HTTP API; WebSocket (future) for live trace updates; fallback polling 1 Hz. State persistence via IndexedDB for offline demo rehearsal.
- **Performance:** Initial load < 3 s over 4G, bundle size < 2 MB gzipped; P95 UI action response < 150 ms. Lighthouse score ≥ 90 mobile/desktop.
- **Testing:** Playwright smoke covering dialog send, memory search; component tests for trace visualiser; visual regression via Storybook Chromatic baseline.

## 7. Knowledge Synthesis Orchestrator
- **Algorithms:**
  - Enumerative search (length-lexicographic) baseline with pruning heuristics and curriculum scheduler.
  - Monte-Carlo Tree Search with rollout policies seeded from top PoE programs and gas-aware reward shaping.
  - Genetic programming operators: crossover, mutation, peephole rewrite library of ≥ 20 templates, partial evaluation, sketch completion.
  - Future plug-ins: simulated annealing, SAT-guided synthesis.
- **Metrics Pipeline:**
  - Compute PoE, MDL, Runtime, GasUsed per candidate; maintain leaderboard persisted to F-KV and mirrored to `/api/v1/orchestrator/leaderboard`.
  - Promotion rule: PoE uplift ≥ 5 % vs parent, MDL non-increasing, runtime within gas budget, reproducible trace hash.
  - Score formula tuneable via config; experiments tracked in Experiment Registry (CSV + dashboard).
- **Automation:** Nightly synthesis runs (8 h), progress dashboards, alert if < 3 programs promoted. On-demand runs triggered via CLI/API respect gas/CPU quotas, writing audit entries to blockchain candidate log.

## 8. Knowledge Blockchain & Network Layer
- **Block Structure:** `{prev_hash, time10, producer_id, program_ids[], PoE_stats, MDL_delta, nonce, signature}` with optional `metadata.compression` hint for payload compaction.
- **Consensus (PoU):** block valid when aggregate PoE ≥ τ (default 0.8) and ≥ 2 peer attestations. Fork resolution chooses chain with highest cumulative PoE then lowest MDL.
- **Networking:**
  - Frames: `HELLO`, `PING`, `PROGRAM_OFFER`, `BLOCK_OFFER`, `FKV_DELTA` with decimal encoding and version negotiation header.
  - Gossip fanout = 3, retry backoff exponential, CRDT OR-Set for offered programs; anti-entropy sync every 30 s.
  - Reputation: score ∈ [0,1], updates per successful validation; nodes < 0.2 throttled, < 0.1 quarantined pending manual review.
- **Security:** Ed25519 signatures, HMAC-SHA256 payload integrity, TLS 1.3 PSK for transport, rate limiting, and anomaly detection on gossip volume.
- **KPIs:** block propagation < 3 s to quorum, fork rate < 2 % weekly, gossip bandwidth < 200 KB/s per peer (95th percentile).
- **Testing:**
  - Simulated 5-node cluster harness, chaos tests (node churn, latency injection), network partitions with heal time SLA < 20 s.
  - Fuzz parsers for network frames, signature verification tests, replay protection checks.

## 9. Delivery, QA, and Readiness
- **Pipelines:** CI stages — lint (clang-tidy, eslint), build, unit (C++/JS), property (VM), fuzz nightly, integration (HTTP/API flows), load/perf weekly. Blocking gates enforce trace determinism hash check and binary size budget.
- **Profiling:** Weekly profiling budget 4 h; capture VM hotspots, trie memory usage, network throughput. Flamegraphs stored in `/docs/perf/` with regression diffs.
- **Documentation:** Maintain living `docs/` set (Architecture, API, Demo Guide, Runbooks) with versioned updates each sprint. Publish changelog and API version matrix.
- **Demo Readiness Checklist:**
  - All API endpoints respond within targets under load test (500 RPS VM, 200 RPS HTTP).
  - Kolibri Studio completes investor demo script with ≤ 2 operator actions per scenario.
  - Backup/restore drill validated (`kolibri.sh demo --replay`).
  - Incident response runbook executed quarterly; RTO < 5 min validated.
- **Governance:** Definition of Ready & Done for each sprint, weekly triage, release sign-off by leads of Core, Synthesis, Network, Product. Release candidate freeze T-7 days with bug bar (no P0/P1 open).

## 10. Milestone Alignment
- **Sprint A (Weeks 1–3):** Focus on Δ-VM, F-KV, HTTP/CLI, Studio v1; exit when KPIs for VM tests, API latency, and web bundle size met.
- **Sprint B (Weeks 4–6):** Ship synthesis orchestrator, blockchain spine, gossip protocols; validate PoE uplift and replication KPIs.
- **Sprint C (Weeks 7–10):** Performance hardening, fuzzing, docs, demo rehearsals; pass soak tests and documentation sign-off.
- **Sprint Ω (Weeks 11–12):** Packaging, pilot onboarding, compliance, patent filing, investor dry-runs.

This blueprint translates the Kolibri Ω vision into actionable, measurable workstreams, ensuring every subsystem is built against explicit KPIs and converges on a world-class public demonstration.
