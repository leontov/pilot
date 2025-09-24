<!-- Copyright (c) 2024 Кочуров Владислав Евгеньевич -->

# Kolibri Ω — Delivery Manual and Agent Playbook

## 0. Scope and Compliance
- This document applies to the entire repository unless a deeper `AGENTS.md` overrides specific folders.
- Follow security-first engineering: prefer constant-time primitives, avoid unchecked buffers, and keep private keys outside of version control.
- Prior to committing, run `make test` and any security tooling referenced in the relevant section. Document all executed commands.
- Keep modifications deterministic and reproducible; describe non-trivial design decisions inside the code using brief comments.

## 1. Vision and Mission
- Deliver a new class of decimal-native intelligence that operates purely on base-10 digits, formulas, and programs rather than neural weights.
- Provide a compact core (≤ 50 MB) that boots in < 300 ms and runs across x86-64 and ARM targets.
- Enable dialogue, self-improvement, knowledge synthesis, and secure federation across a network of peers.
- Ship a world-class demonstration that investors and partners can reproduce end-to-end.

## 2. System Pillars
1. **Δ-VM v2 (Decimal Virtual Machine)** — deterministic stack VM with bounded gas, JSON tracing, and optional threaded/JIT execution.
2. **F-KV v2 (Fractal Knowledge Vault)** — base-10 trie memory offering episodic, semantic, and procedural storage with arithmetic coding.
3. **Knowledge Synthesis Engine** — program-first reasoning using search (length-lexicographic, MCTS, GA, peephole) guided by PoE/MDL metrics.
4. **Knowledge Blockchain** — PoU-governed CRDT chain with Ed25519 signing and HMAC integrity checks.
5. **Swarm Network** — gossip-based decimal frame protocol with rate limiting and reputation scoring.
6. **Kolibri Studio (Web UI)** — Vite/React SPA for dialogue, memory inspection, program tracing, synthesis telemetry, blockchain, cluster, and benchmarks.

## 3. Architecture Deep Dive
### 3.1 Δ-VM v2
- Instruction set: `PUSHd`, `ADD10`, `SUB10`, `MUL10`, `DIV10`, `CMP`, `JZ`, `JNZ`, `CALL`, `RET`, `READ_FKV`, `WRITE_FKV`, `HASH10`, `RANDOM10`, `TIME10`, `HALT`.
- Deterministic stack machine with explicit gas limits to prevent infinite loops.
- Execution traces captured as stepwise JSON (registers, stack, last operations) for debugging and reinforcement.
- Performance target: P95 runtime < 50 ms for 256-step programs on reference hardware.
- Implementation: C-based threaded interpreter with dispatch table and optional JIT for hot paths.

### 3.2 F-KV v2
- Ten-ary trie storing top-K items per prefix: programs, facts, episodic memories.
- API surface: `fkv_put`, `fkv_get_prefix`, `fkv_topk_programs`.
- Supports episodic, semantic, and procedural memory types.
- Uses decimal arithmetic coding for compact persistence; aims for ≥ 1 M keys with P95 `GET` < 10 ms.

### 3.3 Knowledge Synthesis
- Treat knowledge as programs; no weight training.
- Employ search techniques: exhaustive length-lexicographic enumeration, MCTS, genetic operators, peephole optimization, sketch-based synthesis, partial specialization.
- Objective: `Score = W1 * PoE - W2 * MDL - W3 * Runtime - W4 * GasUsed`.
- Promotion pipeline accepts only programs improving PoE while reducing MDL.

### 3.4 Knowledge Blockchain
- Block schema: `{prev_hash, time10, program_ids[], PoE_stats, MDL_delta, nonce}`.
- Consensus: Proof-of-Use (PoU); blocks accepted when PoE ≥ threshold τ.
- Replication via gossip plus CRDT OR-Set merge semantics.
- Security: Ed25519 signatures for authenticity, HMAC-SHA256 for integrity.

### 3.5 Swarm Network
- Frame types: `HELLO`, `PING`, `PROGRAM_OFFER`, `BLOCK_OFFER`, `FKV_DELTA`; all fields encoded as decimal bytes.
- Traffic governance through rate limits and peer reputation computed from useful block ratios.

### 3.6 Kolibri Studio
- React/Vite SPA with tabs for Dialogue, Memory (F-KV Explorer), Programs (editor + Δ-VM trace), Synthesis (search logs), Blockchain (chain/blocks), Cluster (peer health), and Benchmarks.
- Provides live control, observability, and demo orchestration for Kolibri Ω.

## 4. Public HTTP API (v1)
- `POST /api/v1/dialog {input} → {answer, trace}`
- `POST /api/v1/vm/run {program} → {result, trace}`
- `GET /api/v1/fkv/get?prefix=... → {values[], programs[]}`
- `POST /api/v1/program/submit {bytecode} → {PoE, MDL, score}`
- `POST /api/v1/chain/submit {program_id} → {status}`
- `GET /api/v1/health` and `GET /api/v1/metrics` → `{uptime, memory, peers, blocks}`

## 5. Key Performance Indicators
- Core footprint ≤ 50 MB; cold start < 300 ms.
- Latency: P95 response < 50 ms across API endpoints.
- Auto-improvement: nightly PoE growth ≥ 20 %.
- ≥ 90 % of blocks re-validated successfully.
- Knowledge replication across swarm in < 3 s.
- 24 h stability under sanitizers (ASAN/UBSAN clean).

## 6. Roadmap Overview
### Sprint A — Core (3 weeks)
- Deliver Δ-VM v2, F-KV v2.
- Implement PoE/MDL metrics and HTTP API v1.
- Kolibri Studio v1: Dialogue + Memory tabs.
- CI: linters and unit tests for VM/F-KV.

### Sprint B — Network & Synthesis (3 weeks)
- Expand synthesis operators and orchestration.
- Integrate blockchain PoU flow, swarm gossip, and CRDT replication.
- Extend Kolibri Studio with Programs, Synthesis, Blockchain, and Cluster views.
- Harden CI with fuzzing, integration, and security scanning.

### Sprint C — Demo Readiness
- Benchmarking harness, failure-injection, disaster-recovery drills.
- Deployment automation: Docker, signing, SBOM, dependency attestations.
- Documentation set: README Pro, Whitepaper, API Spec, operational runbooks.

## 7. End-to-End Demo Scenarios (Must-Have)
1. **Arithmetic** — Query `2+2` runs via Δ-VM returning `4`.
2. **Memory Write** — "Remember that Moscow is the capital of Russia" persists into F-KV.
3. **Memory Recall** — Retrieval query returns "Moscow".
4. **Program Exchange** — Peer submits PoE = 0.95 program; node validates, blocks, and gossips it.

## 8. Tooling and Repository Layout
- `src/` contains Δ-VM, F-KV, synthesis, blockchain, protocol, and HTTP server modules.
- `include/` hosts public headers, mirroring `src` structure.
- `tests/` comprises unit, property, fuzz, and integration suites; extend coverage with every feature.
- `web/` holds Kolibri Studio (React + Vite).
- `kolibri.sh` orchestrates build, test, deployment, and security workflows.
- Maintain deterministic builds via CMake/Make and lockfile-managed JS deps.

## 9. Quality, Security, and Automation
- Unit tests: cover VM opcodes, F-KV storage semantics, protocol signing, and HTTP handlers.
- Property/fuzz tests: target serialization, delta propagation, and VM execution paths.
- Static analysis: enable `clang-tidy`, `cppcheck`, and sanitizer builds; treat warnings as errors.
- Security automation: dependency scanning, SBOM generation, signature verification, and CI policies enforcing mTLS/JWT, key rotation, and Ed25519 validation.
- Disaster Recovery: maintain runbooks for key rotation, node compromise, and swarm partition handling.

## 10. Team Structure & Processes
- Squads: Core (VM, F-KV), Synthesis & Blockchain, Network, Product/UX, DevOps/QA.
- Engineering standards: mandatory code review, comprehensive tests, profiling before optimization.
- Rituals: weekly demos, burn-down tracking, Kanban visibility, and living documentation in the repo.

## 11. Inputs, Risks, and Mitigations
- Inputs: Kolibri legacy assets, Δ-VM v1/F-KV v1 specs, experienced team.
- Risks: trie performance bottlenecks, synthesis complexity, protocol security.
- Mitigations: prototyping, profiling, fuzzing, security audits, and patent/export compliance checks.

## 12. Definition of Done
- All APIs meet latency, format, and stability targets.
- Kolibri Studio showcases every mandatory scenario.
- Swarm maintains consistent knowledge replication.
- Documentation set (README Pro, Whitepaper, API Spec) complete.
- Performance, load, and security tests executed with artifacts archived.
- Launch materials and automation scripts polished for reproducible demos.

## 13. Executive Summary
Kolibri Ω outlines a cohesive plan for a decimal-native intelligence platform. By adhering to this playbook—spanning VM, memory, synthesis, blockchain, networking, UI, and operational readiness—the team can deliver a reproducible, scalable, and secure product that embodies the "world-class" benchmark for investors and partners.
