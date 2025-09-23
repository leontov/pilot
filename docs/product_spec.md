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

## 2. Δ-VM Core Specification
- **Architecture:** Stack-based deterministic interpreter with decimal opcode set `{PUSHd, ADD10, SUB10, MUL10, DIV10, CMP, JZ, JNZ, CALL, RET, READ_FKV, WRITE_FKV, HASH10, RANDOM10, TIME10, HALT}`.
- **Execution Limits:**
  - Gas limit enforced per program (default 10 000 steps) with configurable policy.
  - Memory limits: stack depth cap 1 024 frames, frame size 256 slots.
- **Performance Targets:** P95 runtime for 256-step program < 50 ms on 3.2 GHz x86-64; JIT optional module must yield ≥ 2× speedup on arithmetic microbenchmarks.
- **Traceability:**
  - JSON trace per instruction: `{pc, opcode, stack_top, gas_remaining, fkv_reads, fkv_writes, timestamp}`.
  - CLI `kolibri.sh trace <program>` dumps trace to `logs/trace.jsonl`.
- **Testing:**
  - Unit coverage for each opcode (happy path + edge cases) ≥ 2 test cases/opcode.
  - Property tests: decimal arithmetic commutativity/associativity (where applicable), gas monotonicity, determinism under identical seeds.
  - Bench harness: `tests/vm/bench_*.c` executed nightly with threshold alarms.

## 3. Fractal KV (F-KV) Specification
- **Structure:** 10-ary trie with node buckets storing top-K (default 8) items sorted by PoE score.
- **APIs:**
  - `fkv_put(key, value, type, metadata)` — writes decimal-encoded value, updates PoE/MDL aggregates.
  - `fkv_get_prefix(prefix, limit, type_filter)` — returns ordered list with latency target P95 < 10 ms.
  - `fkv_topk_programs(prefix, k)` — fetches highest PoE procedural memories.
- **Persistence & Compression:**
  - Write-ahead log + periodic snapshot; arithmetic coding for node payloads targeting 30 %+ compression.
  - GC cadence: reclaim stale entries nightly, ensure disk usage growth < 5 % week-over-week.
- **Testing & Tooling:**
  - Synthetic load generator `kolibri.sh fkv-bench` populates 1 M keys, reports throughput/latency.
  - Consistency checks comparing trie traversal vs serialized form.

## 4. Δ-VM ⇄ F-KV Integration
- **Instruction Semantics:**
  - `READ_FKV`: pops prefix & type, pushes JSON handle referencing retrieved values.
  - `WRITE_FKV`: pops key/value/type, returns status code and gas cost proportional to serialized length.
- **Memory Profiles:** Templates for episodic, semantic, procedural stores with default gas accounting and PoE update rules.
- **Scenarios:** Regression programs covering fact write/readback, procedural program promotion, conflict resolution.
- **Observability:** Trace entries link to F-KV operations (`fkv_op_id`).

## 5. HTTP API v1 & CLI
- **Routes:**
  - `GET /status` → node health, build hash, uptime, memory, peers.
  - `POST /run` → execute Δ-VM program; response includes `result`, `trace_url`, `gas_used`.
  - `POST /dialog` → conversation turn using VM-backed orchestrations.
  - `GET /fkv/prefix` → wrapper for `fkv_get_prefix`.
  - `POST /program/submit` → ingest candidate program with PoE/MDL metrics.
  - `POST /chain/submit` → broadcast knowledge block.
- **Non-functional Targets:** All routes P95 < 75 ms (including network overhead); JSON schema validation; authentication stub via API keys.
- **CLI `kolibri.sh`:**
  - Commands: `up`, `down`, `status`, `trace`, `bench`, `demo` (runs scripted investor demo), `fkv-bench`.
  - Idempotent operations, exit codes documented, logs rotated (< 50 MB per service).

## 6. Kolibri Studio (Vite/React SPA)
- **Tabs & Views:**
  - **Dialog:** chat interface, streaming responses, trace panel linking to VM steps.
  - **Memory:** F-KV explorer with prefix search, PoE/MDL visualisations, writeback actions.
  - **Roadmap:** placeholder for upcoming Synthesis, Blockchain, Cluster dashboards.
- **Data Flow:** React Query fetching from HTTP API; WebSocket (future) for live trace updates.
- **Performance:** Initial load < 3 s over 4G, bundle size < 2 MB gzipped; P95 UI action response < 150 ms.
- **Testing:** Playwright smoke covering dialog send, memory search; component tests for trace visualiser.

## 7. Knowledge Synthesis Orchestrator
- **Algorithms:**
  - Enumerative search (length-lexicographic) baseline with pruning heuristics.
  - Monte-Carlo Tree Search with rollout policies seeded from top PoE programs.
  - Genetic programming operators: crossover, mutation, peephole rewrite library of ≥ 20 templates.
- **Metrics Pipeline:**
  - Compute PoE, MDL, Runtime, GasUsed per candidate; maintain leaderboard persisted to F-KV.
  - Promotion rule: PoE uplift ≥ 5 % vs parent, MDL non-increasing, runtime within gas budget.
- **Automation:** Nightly synthesis runs (8 h), progress dashboards, alert if < 3 programs promoted.

## 8. Knowledge Blockchain & Network Layer
- **Block Structure:** `{prev_hash, time10, producer_id, program_ids[], PoE_stats, MDL_delta, nonce, signature}`.
- **Consensus (PoU):** block valid when aggregate PoE ≥ τ (default 0.8) and ≥ 2 peer attestations.
- **Networking:**
  - Frames: `HELLO`, `PING`, `PROGRAM_OFFER`, `BLOCK_OFFER`, `FKV_DELTA` with decimal encoding.
  - Gossip fanout = 3, retry backoff exponential, CRDT OR-Set for offered programs.
  - Reputation: score ∈ [0,1], updates per successful validation; nodes < 0.2 throttled.
- **Security:** Ed25519 signatures, HMAC-SHA256 payload integrity, TLS 1.3 PSK for transport.
- **KPIs:** block propagation < 3 s to quorum, fork rate < 2 % weekly.
- **Testing:**
  - Simulated 5-node cluster harness, chaos tests (node churn, latency injection).
  - Fuzz parsers for network frames, signature verification tests.

## 9. Delivery, QA, and Readiness
- **Pipelines:** CI stages — lint (clang-tidy, eslint), build, unit (C++/JS), property (VM), fuzz nightly, integration (HTTP/API flows).
- **Profiling:** Weekly profiling budget 4 h; capture VM hotspots, trie memory usage, network throughput.
- **Documentation:** Maintain living `docs/` set (Architecture, API, Demo Guide, Runbooks) with versioned updates each sprint.
- **Demo Readiness Checklist:**
  - All API endpoints respond within targets under load test (500 RPS VM, 200 RPS HTTP).
  - Kolibri Studio completes investor demo script with ≤ 2 operator actions per scenario.
  - Backup/restore drill validated (`kolibri.sh demo --replay`).
- **Governance:** Definition of Ready & Done for each sprint, weekly triage, release sign-off by leads of Core, Synthesis, Network, Product.

## 10. Milestone Alignment
- **Sprint A (Weeks 1–3):** Focus on Δ-VM, F-KV, HTTP/CLI, Studio v1; exit when KPIs for VM tests, API latency, and web bundle size met.
- **Sprint B (Weeks 4–6):** Ship synthesis orchestrator, blockchain spine, gossip protocols; validate PoE uplift and replication KPIs.
- **Sprint C (Weeks 7–10):** Performance hardening, fuzzing, docs, demo rehearsals; pass soak tests and documentation sign-off.
- **Sprint Ω (Weeks 11–12):** Packaging, pilot onboarding, compliance, patent filing, investor dry-runs.

This blueprint translates the Kolibri Ω vision into actionable, measurable workstreams, ensuring every subsystem is built against explicit KPIs and converges on a world-class public demonstration.
