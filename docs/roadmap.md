# Kolibri Ω Development Roadmap

## Guiding Objectives
- Ship a reproducible decimal intelligence node with a self-contained runtime ≤ 50 MB and world-class demo scenarios.
- Deliver incremental value every sprint while keeping Δ-VM, F-KV, synthesis, blockchain, and Kolibri Studio in sync.
- Maintain provable quality bars: PoE uplift ≥ 20 % overnight, P95 latency < 50 ms, 24 h soak stability, and investor-ready demo collateral.

## Timeline at a Glance
| Milestone | Duration | Scope Focus | Exit Criteria |
|-----------|----------|-------------|---------------|
| **M0 — Prototype Core** | Weeks 1–3 | Δ-VM v2 interpreter, F-KV trie, baseline HTTP/CLI, Studio tabs: Dialog & Memory | VM + F-KV unit/property tests green; `/api/v1/dialog` & `/api/v1/fkv/get` stable; core binary ≤ 50 MB |
| **M1 — Networked Intelligence** | Weeks 4–6 | Knowledge synthesis orchestrator, PoE/MDL scoring, knowledge blockchain, gossip protocol, Studio tabs: Synthesis, Blockchain, Cluster | Auto-synthesis produces ≥ 5 promoted programs with PoE ≥ 0.8; blocks replicate across 3 nodes in < 3 s; security review for PoU |
| **M2 — Product Readiness** | Weeks 7–10 | Performance tuning, robustness & fuzzing, observability, documentation suite, investor demo kit | Soak test 24 h without leaks; README Pro + Whitepaper + API spec published; scripted demo passes full 30-scenario playbook |
| **MΩ — Launch & Pilot** | Weeks 11–12 | Packaging, pilot integrations, partner onboarding, patent filings | Docker image ≤ 120 MB; pilot partner runs scripted demo; patent dossier submitted |

## Sprint Breakdown

### Sprint 1 (Weeks 1–2) — Boot the Core
- Wire `KolibriAI` into the node lifecycle, expose `/api/v1/ai/state` & `/api/v1/ai/formulas`, and hydrate default datasets.
- Finalise Δ-VM opcode tests, add property-based coverage for arithmetic and gas handling.
- Implement F-KV persistence stubs (WAL + snapshot hooks) and benchmark prefix queries against the 1 M key target.
- Harden CLI: `./kolibri.sh up|stop|bench|clean` idempotency, log rotation, health checks.
- Deliverables: smoke demo (arithmetic + memory recall), CI pipeline (clang-tidy, unit, property tests), initial Studio tabs (Dialog, Memory) with live traces.

### Sprint 2 (Week 3) — Observability & QA Baseline
- Add structured logging (JSON) and Prometheus metrics endpoints.
- Create minimal load harness (500 RPS VM invocations) and capture latency metrics.
- Author developer docs: environment setup, coding standards, test matrix.
- Review security posture: sandbox validation, syscall audit, fuzz harness seeds.

### Sprint 3 (Weeks 4–5) — Synthesis Factory
- Build enumerative + MCTS hybrid search loop with PoE/MDL evaluation datasets.
- Implement peephole optimiser, sketch filling, partial evaluation pipeline.
- Expose synthesis progress UI (live candidate table, scoring graphs) and API `POST /api/v1/program/submit`.
- Integrate promotion path: successful programs persisted in F-KV, announced over gossip, added to blockchain candidate pool.

### Sprint 4 (Week 6) — Network Spine
- Implement gossip frames (`HELLO`, `PING`, `PROGRAM_OFFER`, `BLOCK_OFFER`, `FKV_DELTA`) with TLS 1.3 PSK and rate limiting.
- Ship PoU validation workflow: re-run programs, verify PoE ≥ τ, compute MDL deltas.
- Launch Blockchain Studio view: block explorer, signature validation, fork detection.
- Run 3-node cluster test: verify replication < 3 s, rejection of low-quality offers, basic reputation scoring.

### Sprint 5 (Weeks 7–8) — Performance & Robustness
- Profile VM interpreter; implement threaded dispatch optimisations and optional peephole JIT for hot paths.
- Optimise F-KV caching (LRU), compression ratios, and snapshot compaction.
- Execute fuzzing campaigns (Δ-VM bytecode, HTTP payloads, gossip frames) with nightly CI jobs.
- Expand testbench: 30 curated scenarios from `docs/demos.md` automated through Kolibri Studio + API scripts.

### Sprint 6 (Weeks 9–10) — Productisation & Demo Readiness
- Finalise documentation suite: README Pro, Whitepaper, Architecture doc, API spec (OpenAPI), Demo handbook.
- Add Observability tab in Studio (metrics dashboards, log streaming, alert surfacing).
- Produce scripted investor demo (video + live walkthrough) and prepare support collateral (FAQ, pricing outline).
- Conduct security audit (penetration test, chain audit) and resolve high/critical findings.

### Sprint 7 (Weeks 11–12) — Launch & Pilot Support
- Package releases: Docker image, binary tarballs, web bundle, seed datasets.
- Establish update channel (chain governance for upgrades, version handshake in gossip).
- Partner onboarding kit: sample integration code, SLA, support workflows.
- Patent filing & compliance checks (export control, licensing review).

## Cross-Cutting Workstreams
- **Quality Assurance:** Maintain ≥ 80 % coverage, nightly fuzz, regression suites for demos and APIs.
- **Security & Compliance:** Threat modelling per milestone, dependency scanning, key rotation policies.
- **Data & Evaluation:** Curate PoE datasets, MDL baselines, golden traces for top demos; refresh quarterly.
- **Developer Experience:** Expand SDK samples, CLI tooling, and Studio UX research with user feedback loops.
- **Community & Partnerships:** Publish technical blog posts, schedule investor updates, and collect pilot feedback to feed Sprint planning.

## Risk Register & Mitigations
| Risk | Impact | Mitigation |
|------|--------|------------|
| Trie performance degradation at scale | High | Early benchmarking, adaptive caching, SIMD-friendly node layout |
| Synthesis search explosion | High | Curriculum tasks, heuristic pruning, reuse of peephole motifs, auto-curriculum tooling |
| Network security threats (Sybil/DoS) | High | TLS everywhere, reputation scoring, rate limiting, quarantine lists, fuzzed parsers |
| Binary footprint growth > 50 MB | Medium | LTO, dead-strip unused code, modular optional features |
| Demo instability under load | Medium | Load rehearsal, failover scripts, real-time monitoring, dry-run drills |

## Checkpoints & Reporting
- Weekly engineering demo + burn-down review.
- Bi-weekly stakeholder update summarising PoE uplift, latency metrics, and risk status.
- Monthly executive report covering milestone readiness, budget burn, hiring needs, and patent progress.

Sticking to this roadmap keeps Kolibri Ω on track for an investor-grade launch while systematically maturing each subsystem and maintaining the zero-weights decimal-first vision.
