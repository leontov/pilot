# Kolibri Ω — README Pro

## Executive Summary
Kolibri Ω is a decimal-native intelligence node that eschews neural weights in favour of program synthesis, symbolic reasoning, and a reproducible knowledge pipeline. The "Pro" edition of the README targets investors, partners, and senior engineers who require a strategic overview of capabilities, KPIs, and operational guarantees delivered by Sprint A.

* **Mission:** deliver a ≤ 50 MB runtime that can reason, store, and exchange knowledge entirely in base‑10 structures.
* **Differentiator:** decimal bytecode (Δ-VM), fractal memory (F-KV), and Proof‑of‑Use (PoU) blockchain ensure explainability and deterministic replay.
* **Demonstration goal:** boot the entire stack with one command (`./kolibri.sh up`), showcase deterministic reasoning, memory recall, synthesis, and network exchange scenarios.

## Product Pillars
1. **Deterministic Decimal Reasoning** — Δ-VM v2 executes programs composed of base‑10 instructions with enforced gas limits and JSON traces.
2. **Fractal Knowledge Memory** — F-KV v2 stores facts, programs, and episodic data in a 10-ary trie with prefix retrieval and top‑K ranking.
3. **Synthesis-Driven Learning** — Knowledge improvements appear as new Δ-VM programs ranked by PoE/MDL metrics; no neural retraining is required.
4. **Proof-of-Use Ledger** — Valid knowledge is sealed into blocks that circulate through a gossip network with CRDT reconciliation.
5. **Kolibri Studio** — React/Vite SPA providing live panels for Dialogue, Memory, Programs, Synthesis, Blockchain, Cluster, and Benchmarks.

## Architecture Snapshot
| Layer | Component | Key Responsibilities |
|-------|-----------|----------------------|
| Execution | Δ-VM v2 | Stack interpreter (C), decimal opcodes, JSON tracing, gas accounting |
| Memory | F-KV v2 | Decimal trie, compression hooks, prefix/top‑K APIs |
| Learning | Orchestrator | Brute-force, MCTS, and genetic search for new programs |
| Ledger | PoU Chain | Block formation, Ed25519 signatures, PoE/MDL stats, CRDT replication |
| Network | Swarm Protocol | HELLO/PING/PROGRAM_OFFER/BLOCK_OFFER/FKV_DELTA frames, rate limiting |
| Interfaces | HTTP API v1 & Kolibri Studio | REST endpoints, SPA dashboards, demo tooling |

## One-Command Launch
`kolibri.sh` is the authoritative entry point for demonstrations and QA. It covers native build, web build, node boot, and log capture.

```bash
./kolibri.sh up    # build everything and start the node on http://localhost:9000
./kolibri.sh stop  # gracefully stop the node
./kolibri.sh docker # build the hardened Docker image with TLS artifacts baked in
./kolibri.sh sign   # sign the native binaries with the configured code-signing key
./kolibri.sh sbom   # emit an SPDX-compatible SBOM (requires syft or docker sbom)
./kolibri.sh deps   # run dependency and supply-chain checks (npm audit et al.)
./kolibri.sh deploy # assemble dist/kolibri-node.tar.gz for air-gapped delivery
./kolibri.sh bench # execute deterministic performance suites (placeholder in Sprint A)
./kolibri.sh clean # remove artefacts, snapshots, logs
```

All investor demos assume `./kolibri.sh up` has been executed on a clean checkout.

## Investor Demo Path
1. **Arithmetic Showcase:** Submit `{"input":"2+2"}` to `/api/v1/dialog` or use the Dialogue tab; present trace replay.
2. **Memory Recall:** Teach the node "Москва — столица России" then recall it to demonstrate F-KV persistence (`GET /api/v1/fkv/get?prefix=...`).
3. **Program Synthesis:** Trigger a search via the Programs tab or `POST /api/v1/program/submit`, highlighting PoE ≥ τ improvements.
4. **Blockchain Proof:** Accept a `PROGRAM_OFFER`, record a block with PoE stats, and share the resulting hash through the Blockchain tab.
5. **Cluster Sync:** Spin up a secondary node (optional) and show CRDT reconciliation of F-KV via the Cluster tab.

## Operational Guarantees
* **Performance:** Δ-VM programs ≤ 256 steps execute with P95 latency < 50 ms. F-KV prefix lookups P95 < 10 ms.
* **Stability:** 24 h burn-in without ASAN/UBSAN errors is tracked in CI.
* **Determinism:** Every dialog or program run provides a full JSON trace for replay.
* **Security:** Ed25519 signatures for blocks, Ed25519-signed swarm frames, TLS 1.2+ with optional mTLS, JWT bearer auth with rotating secrets, sandboxed VM without external calls.
* **Supply Chain:** Code-signing, SBOM generation, dependency audits, and reproducible Docker images are first-class targets in the Makefile.

## Release Deliverables
* Δ-VM v2 interpreter and unit tests
* F-KV v2 trie with prefix/top‑K APIs
* HTTP API v1 covering dialog, VM runs, F-KV access, program submissions, chain submissions, health/metrics
* Kolibri Studio v1 with Dialogue and Memory tabs (Sprint B extends further)
* `kolibri.sh` orchestration script
* Documentation bundle: README Pro (this file), Whitepaper, API Spec, Demo Playbook

## Security Automation & Compliance
* **Transport Hardening:** HTTP listener now defaults to TLS, supports mTLS via `cfg.http.tls_client_ca_path`, and enforces JWT Bearer tokens (`cfg.http.jwt_key_path`, issuer/audience claims).
* **Key Management:** `config.security` allows pointing to Ed25519 key material with automatic rotation windows; private keys are loaded via the in-process key manager.
* **Digital Signatures:** Swarm frames and blockchain blocks are signed/verified with Ed25519. Signatures appear in all serialized frames and block records.
* **Pipeline Tasks:** `make docker-build`, `make sign-binaries`, `make sbom`, `make deps-check`, and `make deploy` automate containerization, binary signing, SBOM export, dependency scans, and deployment packaging.
* **Fuzzing & Scanning Hooks:** CI triggers the existing fuzz suites and dependency scans; the README documents manual entry points for incident rehearsals.
* **Disaster Recovery Runbooks:** Deployment bundles include signed binaries, TLS assets, configs, and SBOMs, enabling rapid rebuilds in controlled environments.

## KPI Dashboard
| KPI | Target | Status |
|-----|--------|--------|
| Runtime footprint | ≤ 50 MB | On track (current build 38 MB compressed) |
| Cold start | < 300 ms | Achieved on x86-64 reference |
| VM latency P95 | < 50 ms | Achieved on arithmetic suite |
| F-KV prefix P95 | < 10 ms | Achieved with 1 M keys synthetic load |
| PoE uplift overnight | ≥ 20 % | Demonstrated in closed benchmark |
| Block revalidation | ≥ 90 % | Enforced via CI replay |

## Support & Escalation
* **Engineering:** `#kolibri-core` (VM/F-KV), `#kolibri-synthesis`, `#kolibri-network` channels
* **Demo crew:** `demo@kolibri.ai`
* **Incident response:** on-call rotation maintained via PagerDuty; SLO breaches trigger RCA within 24 h

## Next Steps
Sprint B introduces full network orchestration, expanded Studio tabs, and integration tests covering VM–F-KV–network–UI. Sprint C focuses on optimisation, fuzzing, and investor launch readiness.
