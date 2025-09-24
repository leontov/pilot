# Kolibri Ω Engineering Playbook

## 1. Purpose and Vision
- **Mission:** Deliver a decimal-native intelligence core that communicates, stores knowledge, and self-improves without neural weights.
- **Constraints:** Core footprint ≤ 50 MB, cold start ≤ 300 ms, deterministic execution, sandboxed runtime, no external LLM dependencies.
- **Definition of Done:** Production-ready demo showing stable dialogue, knowledge synthesis, blockchain replication, full documentation, and automated launch via `kolibri.sh up`.

## 2. System Architecture Guidelines
### 2.1 Δ-VM v2 (Decimal Virtual Machine)
- Instruction set: `PUSHd`, `ADD10`, `SUB10`, `MUL10`, `DIV10`, `CMP`, `JZ`, `JNZ`, `CALL`, `RET`, `READ_FKV`, `WRITE_FKV`, `HASH10`, `RANDOM10`, `TIME10`, `HALT`.
- Implement as a threaded interpreter in C. Enforce gas limits and deterministic control flow.
- Provide step-by-step JSON traces (registers, stack, recent opcodes) for debugging and learning loops.
- Performance goal: P95 runtime < 50 ms for 256-step programs on x86-64/ARM.

### 2.2 F-KV v2 (Fractal Knowledge Vault)
- 10-ary trie storing top-K entries per prefix across episodic, semantic, and procedural memories.
- Public API: `fkv_put`, `fkv_get_prefix`, `fkv_topk_programs`.
- Apply decimal arithmetic compression and target P95 `GET` latency < 10 ms for ≥1 M keys.

### 2.3 Knowledge Synthesis Pipeline
- Treat every knowledge artifact as Δ-VM bytecode.
- Techniques: length-lexicographic search, MCTS, genetic operators, peephole optimizations, sketching, partial evaluation.
- Score function: `Score = W1*PoE – W2*MDL – W3*Runtime – W4*GasUsed`; only promote programs that raise PoE and reduce MDL.

### 2.4 Proof-of-Use Knowledge Blockchain
- Block format: `{ prev_hash, time10, program_ids[], PoE_stats, MDL_delta, nonce }`.
- Consensus: accept blocks when Proof-of-Use ≥ threshold τ; verify PoE calculation via Δ-VM traces.
- Persistence: OR-Set CRDT layers with gossip replication; sign blocks with Ed25519 and protect transport with HMAC-SHA256.

### 2.5 Swarm Network
- Frame types: `HELLO`, `PING`, `PROGRAM_OFFER`, `BLOCK_OFFER`, `FKV_DELTA` (decimal-byte encoding).
- Implement rate limiting, reputation tracking (usefulness ratio), and congestion backoff.

### 2.6 Kolibri Studio (React + Vite)
- Tabs: Dialogue, Memory (F-KV explorer), Programs (editor + VM trace), Synthesis (search logs), Blockchain (chain view + PoE/MDL charts), Cluster (peer status), Benchmarks.
- Tie UI state directly to HTTP API responses and maintain history/metrics dashboards.

## 3. HTTP API v1 Requirements
- `POST /api/v1/dialog` → `{ answer, trace }`
- `POST /api/v1/vm/run` → `{ result, trace }`
- `GET /api/v1/fkv/get?prefix=` → `{ values[], programs[] }`
- `POST /api/v1/program/submit` → `{ PoE, MDL, score }`
- `POST /api/v1/chain/submit` → `{ status }`
- `GET /api/v1/health` and `GET /api/v1/metrics` → uptime, memory usage, peers, blocks, request counters.
- All endpoints respond with deterministic JSON, include error envelopes, and update metrics.

## 4. Testing and Quality Expectations
- Unit coverage for Δ-VM, F-KV, HTTP handlers, blockchain validation, and CRDT replication.
- Integration suite executing VM → F-KV → network → UI path; complete existing scenarios (e.g., `tests/test_blockchain_storage.c`).
- Provide fuzzing harnesses for network parsers and storage encoders; run with ASAN/UBSAN enabled.
- Target 24-hour stability burn runs before releases.

## 5. Continuous Integration
- Extend `ci.yml` with clang-tidy, eslint, ASAN/UBSAN, fuzz jobs, and profiling benchmarks.
- Treat lint/fuzz failures as blockers. Cache dependencies to keep CI ≤ 15 min per pipeline.

## 6. Documentation and Demo Collateral
- Maintain README Pro, Whitepaper, API Spec with up-to-date architecture diagrams and PoU math.
- Deliver investor-ready demo scripts covering arithmetic, knowledge memorization/recall, and network replication.
- Ensure all artefacts build and launch from `./kolibri.sh up`.

## 7. Repository Map
```
/           # Root build scripts and orchestration
  /cfg      # Configuration presets
  /docs     # Specifications, whitepaper, API references
  /include  # Public headers
  /src
    /vm          # Δ-VM implementation
    /fkv         # Fractal memory engine
    /synthesis   # Program search pipeline
    /chain       # Knowledge blockchain
    /protocol    # Network stack
    /http        # HTTP server & routes
  /web      # Kolibri Studio (React + Vite)
  /tests    # Unit, property, fuzz, integration suites
  kolibri.sh
```

## 8. Delivery Checkpoints
1. **Sprint A:** Δ-VM v2, F-KV v2, HTTP API v1, Kolibri Studio v1 (Dialogue + Memory), CI with lint + unit tests.
2. **Sprint B:** Network protocols, synthesis engine, blockchain persistence, Kolibri Studio expansion, CRDT gossip.
3. **Sprint C:** Integration tests, optimization, profiling, security hardening, investor demos.

## 9. Risk Management
- Watch trie performance, synthesis complexity, and protocol security; mitigate with prototypes, profiling, fuzzing, and audits.
- Track legal considerations (patents, export compliance).

## 10. Team Process
- Cross-functional squads: Core (VM/F-KV), Synthesis + Blockchain, Network, Product/UX, DevOps/QA.
- Enforce code review, documentation, static analysis, and burn-down reporting. Hold weekly demos and maintain living design docs.

---
This playbook consolidates Kolibri Ω specifications into actionable guidance for contributors. Follow these directives for any files within this repository subtree.