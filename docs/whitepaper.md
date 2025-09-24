# Kolibri Ω Whitepaper

## Abstract
Kolibri Ω introduces a decimal-native intelligence architecture that replaces neural weights with symbolic program synthesis, deterministic execution, and a Proof-of-Use (PoU) knowledge ledger. This whitepaper documents the problem framing, system design, mathematical underpinnings, performance goals, and roadmap required to deliver a reproducible, world-class demonstration within a ≤ 50 MB footprint.

## 1. Vision and Motivation
* **Explainability:** Every inference step is expressed as Δ-VM bytecode with full traces.
* **Determinism:** Gas limits and sandboxing guarantee bounded execution.
* **Compactness:** Decimal encoding, trie-based storage, and arithmetic compression minimise the runtime footprint.
* **Network-native knowledge:** PoU incentivises sharing validated programs instead of opaque weights.

## 2. System Overview
Kolibri Ω is organised as a stack of modular subsystems communicating over decimal protocols.

### 2.1 Δ-VM v2
* **Instruction Set:** `PUSHd, ADD10, SUB10, MUL10, DIV10, CMP, JZ, JNZ, CALL, RET, READ_FKV, WRITE_FKV, HASH10, RANDOM10, TIME10, HALT`.
* **Execution Model:** Threaded interpreter in C with operand stack, call stack, and deterministic gas counter.
* **Traceability:** After each opcode execution the VM emits a JSON record (program counter, stack snapshot, gas remaining) for debugging and training.
* **Determinism Guarantees:** The VM rejects out-of-gas states, invalid opcodes, and non-decimal memory writes, preventing undefined behaviour.

### 2.2 Fractal Key-Value Store (F-KV v2)
* **Topology:** 10-ary trie addressing decimal prefixes for semantic, episodic, and procedural namespaces.
* **Storage Semantics:** Each node stores top-K entries ranked by PoE and MDL, with arithmetic coding hooks for compression.
* **APIs:** `fkv_put`, `fkv_get_prefix`, `fkv_topk_programs` deliver millisecond-level lookups for millions of keys.
* **Consistency:** Writes are idempotent; CRDT OR-Set deltas enable conflict-free replication across nodes.

### 2.3 Knowledge Synthesis Orchestrator
* **Search Strategies:** Length-lexicographic enumeration, Monte-Carlo Tree Search (MCTS), genetic programming, peephole optimisation, and sketch completion.
* **Evaluation Metrics:**
  - `PoE`: Proof-of-Effectiveness, capturing real-world usefulness and user feedback.
  - `MDL`: Minimum Description Length, rewarding compressed representations.
  - `Runtime`: Average execution time under benchmark load.
  - `GasUsed`: Total gas consumption during evaluation.
  - `Score = W1 * PoE - W2 * MDL - W3 * Runtime - W4 * GasUsed`.
* **Safety:** Only programs with positive PoE deltas and non-increasing MDL are promoted to the canonical set.

### 2.4 Proof-of-Use Blockchain
* **Block Structure:** `{ prev_hash, time10, program_ids[], PoE_stats, MDL_delta, nonce }`.
* **Consensus Rule:** Blocks must demonstrate PoE ≥ τ and valid Ed25519 signatures anchored to rotating node identities. Nodes perform replay to verify Δ-VM execution before accepting blocks.
* **Replication:** Gossip protocol with CRDT OR-Set ensures eventual convergence. Reputation scores penalise spam offers.

### 2.5 Swarm Protocol
* **Frames:** `HELLO`, `PING`, `PROGRAM_OFFER`, `BLOCK_OFFER`, `FKV_DELTA` encoded as decimal strings with attached Ed25519 signatures and signer identifiers.
* **Traffic Control:** Token-bucket rate limiting, peer scoring, and TTLs prevent abuse.
* **Security:** HMAC-SHA256 integrity, mandatory TLS/mTLS for inter-site links, JWT-enforced HTTP access, sandboxed ingress pipeline.

### 2.6 Interfaces
* **HTTP API v1:** REST endpoints for dialogue, VM execution, memory, program submission, chain submission, health, and metrics. All endpoints are served over TLS 1.2+, honour optional mTLS, and require JWT Bearer tokens.
* **Kolibri Studio:** React/Vite SPA with tabs for Dialogue, Memory, Programs, Synthesis, Blockchain, Cluster, Benchmarks. Provides investor-friendly dashboards and trace visualisation.

## 3. Implementation Footprint
* **Language:** C for core runtime; TypeScript/React for Studio.
* **Binary Size:** Target ≤ 50 MB including VM, F-KV, HTTP layer, and static assets.
* **Build System:** CMake/Make orchestrated via `kolibri.sh` for turnkey demos; dedicated targets for Docker images, SBOMs, dependency audits, code-signing, and deployment bundles.
* **CI/CD:** Linting, unit tests (VM, F-KV), fuzz harnesses, integration suites (Sprint B+), automated SBOM publication, dependency scanning, and security policy enforcement.

## 4. Security Model
1. **Sandboxed VM:** No system calls, no floating-point operations, deterministic randomness via decimal RNG.
2. **Cryptography:** Ed25519 keys for signing frames and blocks, HMAC-SHA256 for message integrity, TLS 1.2+ with mTLS for transport, JWT with issuer/audience validation for HTTP auth.
3. **Key Management:** On-disk Ed25519 key material is loaded through a rotating key manager (configurable via `config.security`), ensuring periodic refresh without downtime.
4. **Supply Chain:** Makefile targets produce signed binaries, SBOMs, and deployment bundles; dependency audits surface vulnerable packages prior to release.
5. **Data Governance:** Namespaced F-KV entries separate episodic, semantic, procedural knowledge. Access requires proper prefixes and capability checks.
6. **Auditability:** Full trace logs, block history verification, deterministic replay harness, and documented disaster recovery runbooks.

## 5. Performance Benchmarks
* **Δ-VM Latency:** P95 < 50 ms for 256-step programs on reference hardware (x86‑64, 3.4 GHz).
* **F-KV Retrieval:** Prefix search P95 < 10 ms with 1 M keys and top‑K=32.
* **Synthesis Throughput:** ≥ 10 candidate programs/second during brute-force enumeration, with ability to scale horizontally.
* **Network Propagation:** Knowledge block replication < 3 s across mesh of 8 nodes with 50 ms RTT.

## 6. Demo Suite
Investor demonstrations focus on arithmetic reasoning, memory recall, synthesis, blockchain validation, and cluster synchronisation. The official playbook resides in `docs/demos.md` and assumes `./kolibri.sh up` has been executed.

## 7. Roadmap Highlights
* **Sprint A:** Core runtime (Δ-VM, F-KV), HTTP API v1, Kolibri Studio v1, CI basics, documentation bundle.
* **Sprint B:** Network stack, synthesis optimisations, blockchain integration tests, Studio tabs for Synthesis/Blockchain/Cluster.
* **Sprint C:** Optimisation, fuzzing, long-haul stability, investor dry runs, deployment hardening.
* **Launch:** Deliver reproducible demo environment, publish whitepaper, release API spec, and host investor showcase.

## 8. Conclusion
Kolibri Ω delivers a disciplined alternative to neural systems: fully explainable decimal reasoning, verifiable knowledge exchange, and turnkey demonstrations. The architecture is ready for partner pilots and offers a clear path to scale through synthesis and distributed knowledge curation.
