
# Kolibri Ω Demo Playbook

## Overview

This playbook collects seventy-eight extended demo scenarios that showcase Kolibri Ω as a world-class, fully explainable decimal AI. Each scenario is framed for engineers, product leads, and demo teams: it states the goal, representative inputs, expected node behaviour across subsystems (Δ-VM, F-KV, synthesis, chain), KPIs, and how to run it both from Kolibri Studio and via the public HTTP API.

> **Guiding principles**
> * Everything is decimal-first: programs are Δ-VM bytecode, memory lives in F-KV, metrics are expressed via PoE/MDL.
> * Every demo must be reproducible. Capture traces, PoE deltas, and MDL deltas when preparing investor presentations.
> * Tie each scenario to measurable KPIs so QA can regress them automatically.

For convenience, similar scenarios are grouped. The "Studio" column below highlights the relevant tab, while the "API" column lists the key endpoint(s).

| # | Scenario | Goal | Key KPIs | Studio | API |
|---|----------|------|----------|--------|-----|
| 1 | Arithmetic & Algebra | Test core arithmetic and algebraic solving | VM latency (P95 < 50 ms), 100 % correctness | Dialogue | `POST /api/v1/dialog` |
| 2 | Symbolic Simplification | Verify rule-based algebra without weights | PoE ≥ 0.8 on CAS suite | Programs / Synthesis | `POST /api/v1/program/submit` |
| 3 | Logic Inference | Demonstrate rule chaining | Accuracy ≥ 95 % on logic set | Memory + Dialogue | `POST /api/v1/fkv/get`, `/api/v1/dialog` |
| 4 | Morphology Rules | Apply morphological transformations | Accuracy ≥ 90 % | Dialogue | `POST /api/v1/dialog` |
| 5 | Episodic & Semantic Recall | Persist & retrieve facts | Prefix hit-rate ≥ 99 % | Memory | `GET /api/v1/fkv/get` |
| 6 | Online Adaptation | Reflect user feedback quickly | PoE growth ≥ +20 % per session | Synthesis | `POST /api/v1/program/submit` |
| 7 | Analogical Reasoning | Map structural analogies | ≥ 70 % success | Dialogue | `POST /api/v1/dialog` |
| 8 | Causal Chains | Execute causal reasoning | ≥ 95 % correctness | Dialogue | `POST /api/v1/dialog` |
| 9 | STRIPS-like Planning | Produce action plans | Plan optimality, runtime < 200 ms | Programs / Synthesis | `POST /api/v1/program/submit` |
| 10 | Game Solving | Solve deterministic games | Win-rate > baseline | Benchmarks | `POST /api/v1/program/submit` |
| 11 | Program Synthesis | Generate Δ-VM routines | 100 % correctness on eval set | Programs | `POST /api/v1/program/submit` |
| 12 | MDL Compression | Compress knowledge | ΔMDL < 0 with PoE ≥ τ | Synthesis | `POST /api/v1/program/submit` |
| 13 | Sequence Induction | Infer numeric patterns | ≥ 90 % accuracy | Dialogue / Bench | `POST /api/v1/dialog` |
| 14 | Chain-of-Thought QA | Produce step-by-step answers | Trace completeness 100 % | Dialogue (trace) | `POST /api/v1/dialog` |
| 15 | Self-Debugging | Diagnose VM bugs | Mean diagnosis time < 200 ms | Programs (trace) | `POST /api/v1/vm/run` |
| 16 | Feedback Learning | Improve via ratings | "Like" rate +15 % | Dialogue + Feedback | `POST /api/v1/dialog` |
| 17 | Dialectic Resolution | Reconcile conflicts | PoE(best) > PoE(inputs) | Dialogue | `POST /api/v1/dialog` |
| 18 | Task Batching | Solve task bundles | Throughput ↑, latency/task ↓ | Benchmarks | `POST /api/v1/vm/run` |
| 19 | Contradiction Detection | Detect conflicting facts | Recall ≥ 95 % | Memory | `GET /api/v1/fkv/get` |
| 20 | Reasoning Under Uncertainty | Handle missing facts | Calibrated Brier score | Dialogue | `POST /api/v1/dialog` |
| 21 | Multi-node Consensus | Reach PoU consensus | Sync < 3 s | Cluster | `POST /api/v1/chain/submit` |
| 22 | Reputation & Spam Filtering | Block bad offers | False accept ≤ 1 % | Cluster | `POST /api/v1/program/submit` |
| 23 | Deterministic Replay | Prove determinism | Trace equality 100 % | Observability | `POST /api/v1/vm/run` |
| 24 | Robustness to Perturbations | Ensure stable outputs | Output variance within bounds | Benchmarks | `POST /api/v1/dialog` |
| 25 | Contextful Dialogue | Maintain dialogue state | Coreference ≥ 85 % | Dialogue | `POST /api/v1/dialog` |
| 26 | Knowledge Refactoring | Merge duplicates | Storage ↓ ≥ 30 % w/out PoE loss | Memory | `POST /api/v1/program/submit` |
| 27 | Unit Conversion | Convert accurately | 100 % accuracy | Dialogue | `POST /api/v1/dialog` |
| 28 | Temporal Reasoning | Manage time calculations | 100 % accuracy | Dialogue | `POST /api/v1/dialog` |
| 29 | Peephole Motif Mining | Accelerate synthesis | Synthesis speed +20 % | Synthesis | `POST /api/v1/program/submit` |
| 30 | Explainability Reports | Generate traces & rationale | Report coverage 100 % | Any tab | `POST /api/v1/dialog` + trace |
| 31 | Differentiation & Integration | Solve calculus basics | Symbolic accuracy 100 % | Dialogue | `POST /api/v1/dialog` |
| 32 | Linear Equation Systems | Solve simultaneous equations | Residual ≤ 0 | Dialogue / Programs | `POST /api/v1/dialog` |
| 33 | Combinatorics Counts | Compute combinations/permutations | Correctness across curated set | Dialogue | `POST /api/v1/dialog` |
| 34 | Number Theory Toolkit | Compute primes, GCD/LCM | Accuracy 100 %; latency < 50 ms | Dialogue / Programs | `POST /api/v1/dialog` |
| 35 | Geometry Formulas | Apply geometric rules | Numeric error < 1e-3 | Dialogue | `POST /api/v1/dialog` |
| 36 | Classical Physics | Evaluate Newtonian relations | Correct unit output | Dialogue | `POST /api/v1/dialog` |
| 37 | Chemical Balancing | Balance reactions | PoE ≥ 0.9 on stoichiometry set | Dialogue / Programs | `POST /api/v1/dialog` |
| 38 | Astronomy Facts | Recall planetary metrics | Recall accuracy ≥ 95 % | Memory | `GET /api/v1/fkv/get` |
| 39 | Morphological Parsing | Split words into morphemes | Segmentation F1 ≥ 0.9 | Dialogue | `POST /api/v1/dialog` |
| 40 | Number-to-Words | Verbalize decimals | 100 % lexical correctness | Dialogue | `POST /api/v1/dialog` |
| 41 | Synonym & Antonym Rules | Produce rule-based lexicon | Coverage on curated lexicon ≥ 90 % | Dialogue | `POST /api/v1/dialog` |
| 42 | Grammar Correction | Fix rule-based grammar | Correction accuracy ≥ 90 % | Dialogue | `POST /api/v1/dialog` |
| 43 | Sentence Assembly | Build sentences from tokens | Syntax validity 100 % | Dialogue | `POST /api/v1/dialog` |
| 44 | Rule-based Translation | Translate via mappings | Gloss accuracy ≥ 90 % | Dialogue | `POST /api/v1/dialog` |
| 45 | Mini Sudoku Solver | Solve 5×5 Sudoku | Puzzle completion rate 100 % | Programs / Bench | `POST /api/v1/program/submit` |
| 46 | Tower of Hanoi Planner | Generate optimal moves | Move count = 2ⁿ−1 | Programs | `POST /api/v1/program/submit` |
| 47 | Boolean Logic Evaluator | Evaluate formulas | Truth-table match 100 % | Dialogue | `POST /api/v1/dialog` |
| 48 | Symbolic Diagnosis | Map symptoms to rules | Precision ≥ 0.9 | Dialogue / Memory | `POST /api/v1/dialog` |
| 49 | Rule-based Compliance | Apply policy rules | False negative rate < 5 % | Dialogue | `POST /api/v1/dialog` |
| 50 | Economic Reasoning | Compute balance sheets | Arithmetic correctness; PoE gain | Dialogue | `POST /api/v1/dialog` |
| 51 | Calendar Computations | Compute movable feasts | Date accuracy 100 % | Dialogue | `POST /api/v1/dialog` |
| 52 | Timezone Conversion | Convert times | Offset accuracy 100 % | Dialogue | `POST /api/v1/dialog` |
| 53 | Shortest-path Planner | Solve weighted graphs | Path optimality 100 % | Programs / Bench | `POST /api/v1/program/submit` |
| 54 | Schedule Builder | Satisfy constraints | Constraint satisfaction rate 100 % | Programs / Bench | `POST /api/v1/program/submit` |
| 55 | IQ Sequences | Continue IQ-style sequences | Accuracy ≥ 95 % | Dialogue | `POST /api/v1/dialog` |
| 56 | Raven Matrices (ASCII) | Solve pattern matrices | Correct answer rate ≥ 80 % | Bench | `POST /api/v1/program/submit` |
| 57 | Analogy Completion | Solve IQ analogies | Accuracy ≥ 85 % | Dialogue | `POST /api/v1/dialog` |
| 58 | Loan Calculator | Compute instalments | Error < 0.5 % vs. reference | Dialogue | `POST /api/v1/dialog` |
| 59 | Recipe Planner | Expand ingredient lists | Step completeness 100 % | Dialogue | `POST /api/v1/dialog` |
| 60 | Dosage Calculator | Compute medical doses | Safety checks 100 % | Dialogue | `POST /api/v1/dialog` |
| 61 | Legal Workflow Advisor | Suggest legal actions | Policy adherence 100 % | Dialogue | `POST /api/v1/dialog` |
| 62 | Historical Timeline | Recall event ranges | Accuracy ≥ 95 % | Memory / Dialogue | `POST /api/v1/dialog` |
| 63 | Geography Lookup | Map countries to capitals | Accuracy 100 % | Dialogue | `POST /api/v1/dialog` |
| 64 | Self PoE Assessment | Explain PoE scoring | PoE trace coverage 100 % | Synthesis / Dialogue | `POST /api/v1/dialog` |
| 65 | Next-step Prediction | Anticipate user intent | Hit-rate ≥ 70 % | Dialogue | `POST /api/v1/dialog` |
| 66 | Self Repair Loop | Fix failed programs | Recovery rate ≥ 80 % | Programs / Synthesis | `POST /api/v1/program/submit` |
| 67 | Why-Explanations | Produce causal explanations | Explanation completeness 100 % | Dialogue | `POST /api/v1/dialog` |
| 68 | Decimal Creativity | Generate rhymes & patterns | Novelty with rule compliance | Dialogue | `POST /api/v1/dialog` |
| 69 | Numeric Music Encoding | Map notes to decimal codes | Encoding correctness 100 % | Dialogue / Programs | `POST /api/v1/dialog` |
| 70 | Federated PoU Consensus | Validate distributed blocks | Consensus time < 3 s | Cluster | `POST /api/v1/chain/submit` |
| 71 | Neighbour Reputation | Adjust peer scores | Reputation drift within bounds | Cluster | `POST /api/v1/program/submit` |
| 72 | CRDT Memory Merge | Merge F-KV deltas | Conflict-free convergence | Cluster / Memory | `POST /api/v1/fkv/get` |
| 73 | Network Failover Drill | Handle node drops | Service continuity 100 % | Cluster | `POST /api/v1/health` |
| 74 | Memory Refactoring Automation | Deduplicate knowledge | Storage ↓ ≥ 30 % | Memory | `POST /api/v1/program/submit` |
| 75 | Auto Curriculum Builder | Stage tasks by difficulty | Curriculum PoE uplift ≥ 15 % | Synthesis | `POST /api/v1/program/submit` |
| 76 | Self Benchmarking | Run internal benches | KPI trend tracked | Benchmarks | `POST /api/v1/program/submit` |
| 77 | Knowledge Chain Audit | Verify block history | Hash/signature match 100 % | Block Explorer | `POST /api/v1/chain/submit` |
| 78 | Explainable Failures | Analyse failed runs | Failure explanations logged 100 % | Observability | `POST /api/v1/vm/run` |

---

## Scenario Details

Below, each scenario is documented in more depth: inputs, node behaviour, KPIs, and exact launch instructions.

### 1. Arithmetic & Algebra with Explanations
- **Goal:** Demonstrate deterministic decimal arithmetic and algebraic solving.
- **Inputs:** `2+2`, `234-5`, `x^2-5x+6=0`.
- **Node Behaviour:** Δ-VM compiles expressions to bytecode, executes step-by-step with JSON traces. Solutions are pushed back into F-KV for later reuse.
- **KPIs:** VM latency P95 < 50 ms for ≤256 steps; 100 % correctness on regression set.
- **Run:** Studio → *Dialogue* (enable “Show Trace”); API → `POST /api/v1/dialog` with `{ "input": "2+2" }`.

### 2. Symbolic Mathematics (No Weights)
- **Goal:** Simplify expressions and factor polynomials using rule programs.
- **Inputs:** `simplify((x^2-1)/(x-1))`, `factor(x^2-5x+6)`.
- **Node Behaviour:** Synthesis generates rewriting programs, peephole optimizer compresses them, Δ-VM executes final bytecode.
- **KPIs:** PoE ≥ 0.8 on curated CAS benchmarks; MDL decreases vs. storing raw expansions.
- **Run:** Studio → *Programs* → “Synthesis by Task”; API → submit candidate via `POST /api/v1/program/submit`.

### 3. Logic Inference (Prolog-lite)
- **Goal:** Execute inference such as modus ponens, modus tollens, transitivity.
- **Inputs:** Facts `parent(A,B)`; query `grandparent(A,C)?`.
- **Node Behaviour:** Facts go to F-KV (`S/logic/...`); Δ-VM program loads them and applies inference rules.
- **KPIs:** ≥95 % accuracy on synthetic logic tasks; prefix query latency P95 < 10 ms.
- **Run:** Studio → *Memory* to seed facts, then *Dialogue* to query; API → `POST /api/v1/dialog`.

### 4. Morphology and Word Formation
- **Goal:** Generate inflected forms via decimal rule tables.
- **Inputs:** `прошедшее время от «нести»`.
- **Node Behaviour:** F-KV holds morphological tables; Δ-VM applies rule program to derive form.
- **KPIs:** ≥90 % accuracy on test lexicon; trace clarity.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 5. Semantic & Episodic Memory
- **Goal:** Persist and recall facts and events.
- **Inputs:** `Запомни, что Москва — столица России`, then `Столица России?`.
- **Node Behaviour:** Writes to F-KV (semantic namespace) and retrieves via prefix search.
- **KPIs:** Prefix recall ≥ 99 %; retrieval P95 < 10 ms.
- **Run:** Studio → *Memory*; API → `GET /api/v1/fkv/get?prefix=S/geo/country/RU`.

### 6. Online Behavioural Adaptation
- **Goal:** Learn from explicit user feedback.
- **Inputs:** Conversations with ratings (`нравится/не нравится`).
- **Node Behaviour:** Updates program PoE, reorders top-K candidates, may trigger synthesis.
- **KPIs:** PoE improvement ≥ +20 % in a scripted session; positive feedback ratio rising.
- **Run:** Studio → *Synthesis* → “Online Adapt”; API → `POST /api/v1/program/submit` with rating metadata.

### 7. Analogies and Abstractions
- **Goal:** Solve analogical reasoning prompts.
- **Inputs:** `мозг : нейрон :: город : ?`.
- **Node Behaviour:** Retrieves structural templates from F-KV, synthesizes bridging program.
- **KPIs:** ≥70 % correct on curated analogies; MDL advantage vs. brute-force enumeration.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 8. Causal Reasoning Mini Suite
- **Goal:** Handle simple causal chains and counterfactual checks.
- **Inputs:** `Если дождь → мокро. Дождь. Вывод?`
- **Node Behaviour:** Δ-VM executes rule chaining, cross-checks contradictions in F-KV.
- **KPIs:** ≥95 % correctness; contradiction detection coverage.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 9. STRIPS-like Planning
- **Goal:** Produce minimal action plans from declarative descriptions.
- **Inputs:** `Сделай чай из воды и чайника`.
- **Node Behaviour:** Planner synthesizes procedure; Δ-VM executes verifying gas budget.
- **KPIs:** Plan length optimal; runtime < 200 ms.
- **Run:** Studio → *Programs* → “Plan Task”; API → `POST /api/v1/program/submit`.

### 10. Mind Games (Nim / 15-puzzle mini)
- **Goal:** Showcase search over game state.
- **Inputs:** Initial positions for Nim or 15-puzzle.
- **Node Behaviour:** MCTS over Δ-VM move generator; returns winning line.
- **KPIs:** Win-rate above baseline; runtime within gas limits.
- **Run:** Studio → *Benchmarks*; API → `POST /api/v1/program/submit`.

### 11. Δ-VM Program Synthesis
- **Goal:** Generate reusable decimal routines (e.g., sorting lists).
- **Inputs:** Natural-language or formal spec; e.g., “сортируй список десятичных чисел”.
- **Node Behaviour:** Synthesis enumerates candidates, ranks by Score formula.
- **KPIs:** 100 % correctness on evaluation sets; MDL reduction vs. naive storage.
- **Run:** Studio → *Programs*; API → `POST /api/v1/program/submit`.

### 12. Knowledge Compression via MDL
- **Goal:** Replace fact tables with compact generators.
- **Inputs:** Long sequences or tables exhibiting a rule.
- **Node Behaviour:** Searches for programs with lower MDL than raw data.
- **KPIs:** ΔMDL < 0; PoE ≥ τ.
- **Run:** Studio → *Synthesis* → “Find generator”.

### 13. Inductive Sequences
- **Goal:** Continue patterns such as Fibonacci or arithmetic progressions.
- **Inputs:** `1,2,3,5,8,?`.
- **Node Behaviour:** Candidate program search with scoring; outputs next terms.
- **KPIs:** ≥90 % accuracy; PoE trending upward with more data.
- **Run:** Studio → *Dialogue* or *Benchmarks*; API → `POST /api/v1/dialog`.

### 14. Chain-of-Thought Question Answering
- **Goal:** Provide multi-step reasoning with explanations.
- **Inputs:** `Сколько минут в 3 часах и 15 мин?`
- **Node Behaviour:** Δ-VM traces each sub-step; UI displays structured reasoning.
- **KPIs:** Correctness 100 %; trace coverage 100 %.
- **Run:** Studio → *Dialogue* (enable traces); API → `POST /api/v1/dialog`.

### 15. Self-Debugging Programs
- **Goal:** Diagnose and fix Δ-VM bytecode errors.
- **Inputs:** Faulty program producing incorrect arithmetic.
- **Node Behaviour:** Compares traces against expected; highlights faulty opcode.
- **KPIs:** Average diagnosis time < 200 ms; fix suggestions generated.
- **Run:** Studio → *Programs* → “Trace & Debug”; API → `POST /api/v1/vm/run`.

### 16. Learning from Critique (RL-lite)
- **Goal:** Adjust behaviour with scalar rewards.
- **Inputs:** Dialogue plus user-supplied ratings.
- **Node Behaviour:** Multi-armed bandit policy over candidate programs.
- **KPIs:** Positive-feedback rate improves ≥ 15 %.
- **Run:** Studio → *Dialogue* (with rating controls); API → `POST /api/v1/dialog` with `{ "feedback": "like" }`.

### 17. Thesis–Antithesis–Synthesis
- **Goal:** Merge conflicting directives into a compromise plan.
- **Inputs:** Two opposing goals.
- **Node Behaviour:** Synthesizes mediator program, ensures PoE(best) > PoE(inputs).
- **KPIs:** Combined Score increases; MDL does not regress.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 18. Multitask Batching
- **Goal:** Execute batched tasks efficiently.
- **Inputs:** List of arithmetic/logic queries.
- **Node Behaviour:** Builds generic parameterized program executed once per batch.
- **KPIs:** Throughput uplift; mean latency/task down vs. serial baseline.
- **Run:** Studio → *Benchmarks*; API → `POST /api/v1/vm/run` with batch payload.

### 19. Contradiction Detection
- **Goal:** Spot inconsistent facts in memory.
- **Inputs:** `A>B`, `B>A`.
- **Node Behaviour:** Δ-VM proof search detects conflict, proposes resolution.
- **KPIs:** ≥95 % contradiction recall; false positives low.
- **Run:** Studio → *Memory* integrity check; API → `GET /api/v1/fkv/get`.

### 20. Reasoning with Missing Data
- **Goal:** Produce calibrated answers under uncertainty.
- **Inputs:** Partial fact chains.
- **Node Behaviour:** Generates hypotheses, marks confidence using decimal probabilities.
- **KPIs:** Low Brier score; user-facing explanations of uncertainty.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 21. Multi-node Consensus (Swarm)
- **Goal:** Show Proof-of-Use consensus across nodes.
- **Inputs:** Shared task with diverse local data.
- **Node Behaviour:** Exchanges `PROGRAM_OFFER`, validates PoE, commits block.
- **KPIs:** Consensus < 3 s; ≥90 % blocks reverified.
- **Run:** Studio → *Cluster*; API → `POST /api/v1/chain/submit`.

### 22. Reputation & Spam Filtering
- **Goal:** Throttle low-quality program offers.
- **Inputs:** Stream of low-PoE offers.
- **Node Behaviour:** Reputation system deprioritizes offenders; rate limits triggered.
- **KPIs:** False accept ≤ 1 %; network throughput stable.
- **Run:** Studio → *Cluster* (“Noise”); API → `POST /api/v1/program/submit`.

### 23. Deterministic Replay
- **Goal:** Verify reproducibility under fixed seeds.
- **Inputs:** Known program with `RANDOM10` seeded.
- **Node Behaviour:** Ensures identical traces across runs.
- **KPIs:** Trace equivalence 100 %; reproducibility logs archived.
- **Run:** Studio → *Observability* → “Replay”; API → `POST /api/v1/vm/run`.

### 24. Stability Under Perturbations
- **Goal:** Measure robustness to small input changes.
- **Inputs:** Slightly perturbed queries.
- **Node Behaviour:** Applies Score penalties for unstable outputs.
- **KPIs:** Output variance within defined bounds; MDL steady.
- **Run:** Studio → *Benchmarks* → “Robustness”.

### 25. Context-Aware Dialogue
- **Goal:** Maintain dialogue state, resolve pronouns.
- **Inputs:** Multi-turn conversations with references.
- **Node Behaviour:** Episodic memory tracks context, semantic lookup resolves entities.
- **KPIs:** Coreference accuracy ≥ 85 %; traceable state transitions.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog` (session token).

### 26. Knowledge Refactoring
- **Goal:** Merge duplicate memory entries.
- **Inputs:** Similar facts/programs in F-KV.
- **Node Behaviour:** MDL-driven compaction merges keys and updates top-K caches.
- **KPIs:** Storage reduction ≥ 30 % without PoE drop.
- **Run:** Studio → *Memory* → “Compact”; API → `POST /api/v1/program/submit`.

### 27. Unit Conversion & Dimensional Checks
- **Goal:** Convert units via decimal formulas.
- **Inputs:** `5 км/ч в м/с`.
- **Node Behaviour:** Applies stored conversion programs; cross-validates dimensions.
- **KPIs:** 100 % accuracy; runtime < 50 ms.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 28. Temporal Reasoning
- **Goal:** Calculate dates/durations.
- **Inputs:** `Какой день через 100 дней?`
- **Node Behaviour:** Uses `TIME10` and arithmetic to compute results.
- **KPIs:** 100 % correctness; reproducible traces.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 29. Peephole Motif Mining
- **Goal:** Extract reusable bytecode motifs to speed synthesis.
- **Inputs:** Corpus of successful programs.
- **Node Behaviour:** Mine frequent subsequences, populate peephole library.
- **KPIs:** Synthesis throughput +20 %; MDL improvements on new tasks.
- **Run:** Studio → *Synthesis* → “Motif Mining”; API → `POST /api/v1/program/submit`.

### 30. Explainability (X-Ray Reports)
- **Goal:** Produce full rationale reports for any answer.
- **Inputs:** Any query requiring explanation.
- **Node Behaviour:** Compiles chain of applied rules, references F-KV entries, attaches block proofs.
- **KPIs:** Report coverage 100 %; generation < 100 ms.
- **Run:** Studio → any tab via “Explain” toggle; API → `POST /api/v1/dialog` with `{ "explain": true }`.

### 31. Differentiation & Integration
- **Goal:** Demonstrate rule-based calculus (first derivatives, simple integrals).
- **Inputs:** `d/dx (x^2+3x)`, `∫ (2x) dx`.
- **Node Behaviour:** Synthesis selects symbolic rules stored in F-KV; Δ-VM applies power/linearity laws with trace justification.
- **KPIs:** Symbolic accuracy 100 %; trace highlights applied identities; runtime < 60 ms.
- **Run:** Studio → *Dialogue* (enable trace); API → `POST /api/v1/dialog` with `{ "input": "d/dx (x^2+3x)" }`.

### 32. Linear Equation Systems
- **Goal:** Solve simultaneous linear equations over decimals.
- **Inputs:** `x+2y=5`, `x-y=1`.
- **Node Behaviour:** Δ-VM executes Gaussian-elimination bytecode generated by synthesis; intermediate matrices remain decimal-only.
- **KPIs:** Residual ≤ 10⁻⁹ on verification; runtime < 80 ms; MDL lower than storing lookup pairs.
- **Run:** Studio → *Programs* → “Solve system”; API → `POST /api/v1/dialog` with JSON array of equations.

### 33. Combinatorics Counts
- **Goal:** Compute combinations/permutations using factorial programs.
- **Inputs:** `C(10,3)`, `P(6,2)`.
- **Node Behaviour:** Δ-VM reuses factorial and product motifs; F-KV caches results for reuse.
- **KPIs:** 100 % correctness on curated set; gas usage within 128.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog` with `{ "input": "C(10,3)" }`.

### 34. Number Theory Toolkit
- **Goal:** Evaluate primes, GCD, LCM, and divisibility properties.
- **Inputs:** `НОД(252,105)`, `LCM(15,20)`.
- **Node Behaviour:** Runs Euclidean algorithm bytecode; optional sieve program for primality stored under `P/math/prime`.
- **KPIs:** 100 % accuracy; VM latency P95 < 50 ms for ≤256 steps.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 35. Geometry Formulas
- **Goal:** Apply decimal approximations of geometric constants and formulas.
- **Inputs:** `Площадь круга радиуса 5`, `Периметр квадрата со стороной 4`.
- **Node Behaviour:** Δ-VM pulls π approximation from F-KV, executes formula program, provides rounding trace.
- **KPIs:** Absolute error < 1e-3 vs. analytic value; trace records formula provenance.
- **Run:** Studio → *Dialogue* with explain toggle; API → `POST /api/v1/dialog`.

### 36. Classical Physics
- **Goal:** Evaluate Newtonian relations (F=ma, W=Fs, etc.).
- **Inputs:** `F=ma, m=2, a=3`.
- **Node Behaviour:** Rule engine selects correct formula template; Δ-VM enforces dimensional annotations stored in F-KV.
- **KPIs:** Correct unit inference; 100 % numeric accuracy on curated sheet.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 37. Chemical Balancing
- **Goal:** Balance stoichiometric equations without floating weights.
- **Inputs:** `H2 + O2 -> H2O`.
- **Node Behaviour:** Linear-diophantine solver synthesised in Δ-VM ensures integer coefficients; writes balanced form to F-KV.
- **KPIs:** PoE ≥ 0.9 on stoichiometry dataset; MDL reduced vs. storing raw pairs.
- **Run:** Studio → *Programs* → “Balance reaction”; API → `POST /api/v1/dialog`.

### 38. Astronomy Facts
- **Goal:** Recall planetary periods and constants.
- **Inputs:** `Сутки на Марсе?`, `Орбита Земли?`.
- **Node Behaviour:** Semantic namespace `S/astro/...` returns stored values; Δ-VM formats decimals per request.
- **KPIs:** Recall accuracy ≥ 95 %; prefix lookup latency < 10 ms.
- **Run:** Studio → *Memory* search; API → `GET /api/v1/fkv/get?prefix=S/astro`.

### 39. Morphological Parsing
- **Goal:** Segment words into morphemes using rule tables.
- **Inputs:** `переписывающийся`.
- **Node Behaviour:** F-KV stores affix tables; Δ-VM program greedily matches sequences and emits segmentation trace.
- **KPIs:** Segmentation F1 ≥ 0.9 on evaluation lexicon.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 40. Number-to-Words Conversion
- **Goal:** Convert large decimals to grammatical text.
- **Inputs:** `12345`.
- **Node Behaviour:** Δ-VM traverses digit groups using F-KV dictionary for thousands/millions; ensures grammatical gender.
- **KPIs:** 100 % lexical correctness up to 10¹²; runtime < 70 ms.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 41. Synonym & Antonym Rules
- **Goal:** Provide curated synonyms/antonyms using rule-based transformations.
- **Inputs:** `большой`.
- **Node Behaviour:** F-KV stores semantic relations; Δ-VM filters by polarity tags (syn/ant) and PoE ranking.
- **KPIs:** Coverage ≥ 90 % on curated lexicon; explanations cite rule IDs.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 42. Grammar Correction
- **Goal:** Apply grammar rules to erroneous sentences.
- **Inputs:** `Он идти домой вчера`.
- **Node Behaviour:** Δ-VM loads rewrite program with tense/person rules; outputs corrected sentence plus change log.
- **KPIs:** Correction accuracy ≥ 90 %; MDL improvement vs. storing fixed strings.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 43. Sentence Assembly
- **Goal:** Build well-formed sentences from token lists.
- **Inputs:** `[собака, бежать, быстро]`.
- **Node Behaviour:** Rule engine orders tokens via dependency templates stored in F-KV.
- **KPIs:** Syntax validity 100 %; latency < 40 ms.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog` with token array.

### 44. Rule-based Translation
- **Goal:** Translate short phrases using deterministic mapping tables.
- **Inputs:** `привет` → `hello`.
- **Node Behaviour:** Δ-VM consults bilingual dictionary in F-KV, applies morphological agreements if needed.
- **KPIs:** Gloss accuracy ≥ 90 % on seed set; trace lists dictionary keys.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 45. Mini Sudoku Solver
- **Goal:** Solve 5×5 Sudoku using constraint propagation.
- **Inputs:** ASCII grid with zeros for blanks.
- **Node Behaviour:** Synthesis produces backtracking + constraint checks; Δ-VM ensures gas guard per branch.
- **KPIs:** Puzzle completion rate 100 %; runtime < 200 ms per puzzle.
- **Run:** Studio → *Benchmarks* → “Mini Sudoku”; API → `POST /api/v1/program/submit` with puzzle payload.

### 46. Tower of Hanoi Planner
- **Goal:** Generate optimal move list for N=3 discs.
- **Inputs:** `N=3`.
- **Node Behaviour:** Recursive bytecode executes CALL/RET, appends moves to trace.
- **KPIs:** Move count equals 2ⁿ−1; gas usage minimal.
- **Run:** Studio → *Programs*; API → `POST /api/v1/program/submit`.

### 47. Boolean Logic Evaluator
- **Goal:** Evaluate boolean expressions with decimal truth values.
- **Inputs:** `(A∧B)∨¬C`, assignments `A=1,B=0,C=1`.
- **Node Behaviour:** Parser program converts to postfix and evaluates via stack operations.
- **KPIs:** Truth-table match 100 %; latency < 30 ms.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 48. Symbolic Diagnosis
- **Goal:** Map symptom combinations to possible diagnoses.
- **Inputs:** `кашель`, `жар`.
- **Node Behaviour:** F-KV stores rule graph linking symptoms to conditions with PoE weights; Δ-VM returns ranked list.
- **KPIs:** Precision ≥ 0.9 on curated set; explanation includes contributing rules.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 49. Rule-based Compliance
- **Goal:** Enforce policy rules (e.g., age restrictions).
- **Inputs:** `Возраст < 18`, `Покупка алкоголя`.
- **Node Behaviour:** Δ-VM evaluates rule tree stored in F-KV, outputs verdict and violated clauses.
- **KPIs:** False negative rate < 5 %; audit trace completeness 100 %.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 50. Economic Reasoning
- **Goal:** Calculate profit/loss and other financial metrics.
- **Inputs:** `Доход 1000, расходы 700`.
- **Node Behaviour:** Δ-VM executes ledger program; stores summary in F-KV for follow-up analytics.
- **KPIs:** Arithmetic correctness; PoE gain vs. baseline heuristics.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 51. Calendar Computations
- **Goal:** Compute movable feasts and date offsets.
- **Inputs:** `Дата Пасхи 2025`.
- **Node Behaviour:** Uses computus algorithm encoded in Δ-VM; verifies via stored historical table.
- **KPIs:** Date accuracy 100 %; runtime < 80 ms.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 52. Timezone Conversion
- **Goal:** Convert times across zones.
- **Inputs:** `10:00 МСК → GMT`.
- **Node Behaviour:** Δ-VM subtracts offsets from F-KV zone table; ensures DST flags respected.
- **KPIs:** Offset accuracy 100 %; trace records offset source.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 53. Shortest-path Planner
- **Goal:** Find minimal path in weighted graphs.
- **Inputs:** Decimal adjacency matrix or edge list.
- **Node Behaviour:** Δ-VM runs Dijkstra-like routine using heap encoded as base-10 arrays.
- **KPIs:** Path optimality 100 %; runtime < 150 ms for 10-node graphs.
- **Run:** Studio → *Benchmarks* → “Graph planner”; API → `POST /api/v1/program/submit`.

### 54. Schedule Builder
- **Goal:** Generate feasible schedules under constraints.
- **Inputs:** Tasks with durations and exclusivity rules.
- **Node Behaviour:** Constraint solver enumerates assignments; F-KV caches successful templates.
- **KPIs:** Constraint satisfaction 100 %; PoE improvement vs. naive order.
- **Run:** Studio → *Benchmarks*; API → `POST /api/v1/program/submit`.

### 55. IQ Sequences
- **Goal:** Continue IQ-style numeric sequences.
- **Inputs:** `2,4,8,16,?`.
- **Node Behaviour:** Candidate generator tests multiplicative patterns, selects highest PoE rule.
- **KPIs:** Accuracy ≥ 95 % on curated benchmark.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 56. Raven Matrices (ASCII)
- **Goal:** Solve simplified Raven matrices encoded as ASCII patterns.
- **Inputs:** 3×3 grid with missing element and candidate options.
- **Node Behaviour:** Δ-VM compares pattern transformations; F-KV stores motif rules.
- **KPIs:** Correct answer rate ≥ 80 %; trace lists applied transforms.
- **Run:** Studio → *Benchmarks*; API → `POST /api/v1/program/submit`.

### 57. Analogy Completion (IQ Style)
- **Goal:** Finish analogies like `Кошка : Котёнок :: Собака : ?`.
- **Inputs:** Structured analogy prompt.
- **Node Behaviour:** Semantic relations from F-KV; Δ-VM ensures consistent relation mapping.
- **KPIs:** Accuracy ≥ 85 %; explanation cites relation path.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 58. Loan Calculator
- **Goal:** Compute annuity or differentiated loan payments.
- **Inputs:** `Сумма 100000`, `Ставка 10%`, `12 месяцев`.
- **Node Behaviour:** Δ-VM executes amortisation formula; stores schedule optionally.
- **KPIs:** Error < 0.5 % vs. financial reference; runtime < 70 ms.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 59. Recipe Planner
- **Goal:** Produce step-by-step recipes from ingredient lists.
- **Inputs:** `Блины: 2 яйца, 1 стакан молока, 1 стакан муки`.
- **Node Behaviour:** Procedural template from F-KV expands into ordered steps with timing; Δ-VM ensures ingredient scaling stays decimal.
- **KPIs:** Step coverage 100 %; PoE validated via taste-test dataset proxy.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 60. Dosage Calculator
- **Goal:** Convert prescriptions to daily totals and schedules.
- **Inputs:** `10 мг × 3 раза/сут`.
- **Node Behaviour:** Δ-VM multiplies dosage, checks max thresholds stored in F-KV safety table.
- **KPIs:** Safety violations = 0; computed totals accurate within rounding tolerance.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 61. Legal Workflow Advisor
- **Goal:** Suggest actions based on legal status.
- **Inputs:** `Срок договора истёк`.
- **Node Behaviour:** Rule engine maps to `Продлить`/`Расторгнуть` decision tree with citations.
- **KPIs:** Policy adherence 100 %; explanation references statute IDs in F-KV.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 62. Historical Timeline
- **Goal:** Recall start/end dates for major events.
- **Inputs:** `Великая Отечественная война`.
- **Node Behaviour:** Semantic recall from `S/history/...`; Δ-VM formats range and attaches block provenance.
- **KPIs:** Accuracy ≥ 95 %; retrieval P95 < 10 ms.
- **Run:** Studio → *Memory* search; API → `POST /api/v1/dialog`.

### 63. Geography Lookup
- **Goal:** Map countries to capitals and related stats.
- **Inputs:** `Канада`.
- **Node Behaviour:** F-KV returns `Оттава`; Δ-VM optionally attaches population/continent facts.
- **KPIs:** Accuracy 100 % on ISO country list; latency < 10 ms.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 64. Self PoE Assessment
- **Goal:** Expose PoE computation for a given task.
- **Inputs:** Scenario ID or recent program hash.
- **Node Behaviour:** Δ-VM recalculates PoE contributions and explains Score weights W1–W4.
- **KPIs:** Trace coverage 100 %; explains each dataset contribution.
- **Run:** Studio → *Synthesis* metrics; API → `POST /api/v1/dialog` with `{ "inspect_poe": "task_id" }`.

### 65. Next-step Prediction
- **Goal:** Predict the user's next intent in dialogue.
- **Inputs:** Conversation history (≥3 turns).
- **Node Behaviour:** Behavioural heuristics in F-KV compute probabilities; Δ-VM outputs ranked intents.
- **KPIs:** Hit-rate ≥ 70 % on replay logs; predictions stored for evaluation.
- **Run:** Studio → *Dialogue* (insight panel); API → `POST /api/v1/dialog` with `{ "predict_next": true }`.

### 66. Self Repair Loop
- **Goal:** Detect and fix faulty programs automatically.
- **Inputs:** Program ID with failing test case.
- **Node Behaviour:** Self-debug pipeline diff traces vs. oracle, synthesizes patch, updates F-KV entry.
- **KPIs:** Recovery rate ≥ 80 %; time-to-fix < 5 min budget.
- **Run:** Studio → *Programs* → “Self-repair”; API → `POST /api/v1/program/submit` with failure context.

### 67. Why-Explanations
- **Goal:** Provide human-readable justification for facts (`Почему 2+2=4?`).
- **Inputs:** Query requiring explanation.
- **Node Behaviour:** Builds inference chain referencing axioms and previous steps.
- **KPIs:** Explanation completeness 100 %; PoE unaffected.
- **Run:** Studio → *Dialogue* (Explain toggle); API → `POST /api/v1/dialog` with `{ "why": "2+2=4" }`.

### 68. Decimal Creativity
- **Goal:** Generate creative outputs (rhymes, simple poetry) using decimal structures.
- **Inputs:** Prompt word `свет`.
- **Node Behaviour:** Δ-VM uses rhyme dictionaries encoded as decimal codes; ensures meter via syllable counts in F-KV.
- **KPIs:** Novelty with rule compliance; PoE from user feedback > baseline.
- **Run:** Studio → *Dialogue*; API → `POST /api/v1/dialog`.

### 69. Numeric Music Encoding
- **Goal:** Map note sequences to decimal/MIDI-like codes.
- **Inputs:** `C D E F G`.
- **Node Behaviour:** Δ-VM converts notes to frequency ratios, outputs decimal-coded sequence for external synthesizer.
- **KPIs:** Encoding correctness 100 %; optional playback verifying order.
- **Run:** Studio → *Programs* → “Music encode”; API → `POST /api/v1/dialog`.

### 70. Federated PoU Consensus
- **Goal:** Demonstrate multiple nodes agreeing on a block.
- **Inputs:** Cluster of 3 nodes exchanging high-PoE program.
- **Node Behaviour:** Gossip `PROGRAM_OFFER`, verify PoE locally, append block, broadcast `BLOCK_OFFER`.
- **KPIs:** Consensus time < 3 s; ≥90 % revalidation success.
- **Run:** Studio → *Cluster*; API → `POST /api/v1/chain/submit`.

### 71. Neighbour Reputation
- **Goal:** Adjust peer reputation based on behaviour.
- **Inputs:** Peer sending spam offers.
- **Node Behaviour:** Reputation scores updated via Δ-VM policy; affects rate limits.
- **KPIs:** Reputation drift bounded; spam acceptance ≤ 1 %.
- **Run:** Studio → *Cluster* → “Reputation”; API → `POST /api/v1/program/submit` with low PoE.

### 72. CRDT Memory Merge
- **Goal:** Merge concurrent F-KV updates via CRDT OR-Set.
- **Inputs:** Two nodes editing overlapping prefixes.
- **Node Behaviour:** Exchange `FKV_DELTA` frames; Δ-VM reconciles ensuring commutativity.
- **KPIs:** Conflict-free convergence; no lost updates; latency < 2 s.
- **Run:** Studio → *Cluster* or *Memory* diff view; API → `POST /api/v1/fkv/get` after sync.

### 73. Network Failover Drill
- **Goal:** Show resilience when a node drops.
- **Inputs:** Simulated node failure event.
- **Node Behaviour:** Remaining nodes rebalance peers, continue gossip; HTTP `/health` reflects degraded peer count.
- **KPIs:** Service continuity 100 %; recovery < 30 s once node returns.
- **Run:** Studio → *Cluster* failover toggle; API → monitor `GET /api/v1/health`.

### 74. Memory Refactoring Automation
- **Goal:** Automatically deduplicate F-KV entries.
- **Inputs:** Memory namespace with near-duplicate facts.
- **Node Behaviour:** MDL-driven analyzer merges keys, updates indexes, writes audit block.
- **KPIs:** Storage reduction ≥ 30 % with unchanged PoE.
- **Run:** Studio → *Memory* → “Refactor”; API → `POST /api/v1/program/submit`.

### 75. Auto Curriculum Builder
- **Goal:** Order tasks from simple to complex to improve synthesis PoE.
- **Inputs:** Task set with difficulty metadata.
- **Node Behaviour:** Scheduler sorts tasks by historical PoE/MDL slope; triggers synthesis accordingly.
- **KPIs:** Curriculum PoE uplift ≥ 15 %; runtime overhead minimal.
- **Run:** Studio → *Synthesis* → “Curriculum”; API → `POST /api/v1/program/submit` with task batch.

### 76. Self Benchmarking
- **Goal:** Let the node run benchmark suites autonomously.
- **Inputs:** `bench_id` list.
- **Node Behaviour:** Δ-VM executes stored benchmark programs, logs metrics, updates dashboard.
- **KPIs:** KPI trend tracked; benchmarks complete without operator intervention.
- **Run:** Studio → *Benchmarks* → “Self run”; API → `POST /api/v1/program/submit`.

### 77. Knowledge Chain Audit
- **Goal:** Audit block history for integrity.
- **Inputs:** Block range or hash.
- **Node Behaviour:** Δ-VM recomputes hashes, verifies Ed25519 signatures, cross-checks PoE stats.
- **KPIs:** Hash/signature match 100 %; discrepancies logged.
- **Run:** Studio → *Blockchain* → “Audit”; API → `POST /api/v1/chain/submit` with `{ "audit": true }`.

### 78. Explainable Failures
- **Goal:** Analyse why a program failed to produce output.
- **Inputs:** Program trace with error status.
- **Node Behaviour:** Self-debug pipeline inspects trace, pinpoints failing opcode, logs fix suggestions.
- **KPIs:** Failure explanations logged 100 %; time-to-diagnose < 2 min.
- **Run:** Studio → *Observability* → “Failure inspector”; API → `POST /api/v1/vm/run` with `{ "debug": true }`.

---

## Next Steps

1. **Automation assets:** Prepare JSON playbooks for each scenario (`/docs/demos/*.json`) so the Studio can fire demos via a single button. Each playbook should define required preconditions (memory seeds, synthesis budget).
2. **Golden traces:** Capture canonical traces, PoE/MDL metrics, and block hashes for regression, storing them in `/tests/integration/demos/`.
3. **Presentation kit:** Create slide snippets and video captures tied to the scenarios above for investor demos.
4. **Continuous verification:** Integrate key KPIs into CI (nightly synthesis regression, memory hit-rate checks) so demos remain green.

Maintain this document as the authoritative reference for demo readiness across product, engineering, and GTM teams.

# 🌐 Полный каталог демо-сценариев Kolibri Ω

Коллекция структурирована древом «категория → подкатегория → пример», чтобы покрыть весь спектр человеческих задач, которые Kolibri Ω должен уверенно демонстрировать. Каждая ветка включает контрольные примеры и минимальный API-протокол для воспроизводимой проверки.

## 1. Математика и вычисления

### 1.1 Арифметика
- Простые операции: сложение, вычитание, умножение, деление.
- Расширенные операции: возведение в степень, извлечение корня, факториал.
- Контрольное демо: расчёт скидки и НДС по чек-листу.
- API: `POST /api/v1/vm/run` с программой Δ-VM, вычисляющей выражение, → `{ "result": "42" }`.

### 1.2 Алгебра
- Линейные уравнения (одна переменная).
- Квадратные и полиномиальные уравнения (до 4 степени).
- Системы уравнений (2–4 переменных).
- Неравенства и промежутки решений.
- API: `POST /api/v1/dialog { "input": "Реши x^2-5x+6=0" }` → `{ "answer": "x1=2, x2=3", "trace": ... }`.

### 1.3 Геометрия
- Планиметрия: периметр, площадь, свойства треугольников и окружностей.
- Стереометрия: объёмы и площади поверхностей тел.
- Геометрические доказательства (теорема Пифагора, Фалеса и т.п.).
- Построения и координатная геометрия.
- API: `POST /api/v1/dialog { "input": "Найди площадь треугольника со сторонами 3,4,5" }`.

### 1.4 Тригонометрия
- Табличные значения sin/cos/tan.
- Решение тригонометрических уравнений.
- Преобразования и формулы (угловые суммы, двойные углы).
- Анализ периодичности и фаз.
- API-пример: `POST /api/v1/dialog { "input": "sin(π/6)" }` → `{ "answer": "0.5" }`.

### 1.5 Линейная алгебра
- Матрицы: сложение, умножение, транспонирование.
- Определители и обратные матрицы.
- Собственные значения и собственные векторы.
- Решение Ax = b методом Гаусса.
- API: `POST /api/v1/vm/run` с программой, вычисляющей детерминант 3×3.

### 1.6 Комбинаторика
- Перестановки, размещения, сочетания.
- Формула включений-исключений.
- Разбиение множеств, числа Белла и Стирлинга.
- Подсчёт путей на решётке.
- API: `POST /api/v1/dialog { "input": "Сколько способов рассадить 5 людей по 3 креслам?" }`.

### 1.7 Теория чисел
- Проверка числа на простоту.
- Факторизация до ограниченного диапазона.
- НОД/НОК, расширенный алгоритм Евклида.
- Конгруэнции и теорема Ферма.
- API: `POST /api/v1/dialog { "input": "НОД(1071, 462)" }`.

### 1.8 Вычислительная математика
- Итерационные методы (Ньютон, бисекция).
- Аппроксимация функций (полиномы, сплайны).
- Численное интегрирование и дифференцирование.
- Ошибки округления и оценка точности.
- API: `POST /api/v1/vm/run` с программой, выполняющей метод Ньютона.

### 1.9 Вероятности и статистика
- Базовые вероятности и условные вероятности.
- Распределения (Бернулли, Биномиальное, Нормальное).
- Мат. ожидание, дисперсия, мода, медиана.
- Проверка гипотез и доверительные интервалы.
- API: `POST /api/v1/dialog { "input": "Вероятность выпадения ≥4 очков при броске кубика" }`.

### 1.10 Криптография и кодирование
- Примеры RSA/Эль-Гамаля на малых числах.
- Хеш-функции (SHA-256) и проверка целостности.
- Кодирование/декодирование (Хэмминг, CRC10).
- Генерация и валидация цифровых подписей (Ed25519).
- API: `POST /api/v1/program/submit { "bytecode": "..." }` → `{ "PoE": 0.91 }`.

## 2. Логика и рассуждения

### 2.1 Булева алгебра
- Таблицы истинности.
- Минимизация булевых функций (Карано/Марк-Квайн).
- Комбинирование логических выражений.
- API: `POST /api/v1/dialog { "input": "Упростить (A∧B)∨(A∧¬B)" }`.

### 2.2 Логические задачи
- Классические «Кто где живёт» с таблицами истинности.
- Судоку, кэн-кен, латинские квадраты.
- Загадки на переправы, перевесы, ковбои и шляпы.
- API: `POST /api/v1/dialog { "input": "Реши судоку ..." }`.

### 2.3 Аналогии и обобщения
- Аналогии словесные и числовые.
- Анализ паттернов: «1,4,9,16 → ?».
- Индуктивные рассуждения.
- API: `POST /api/v1/dialog { "input": "Какое число продолжит последовательность 2,5,10,17?" }`.

### 2.4 Силлогизмы и дедукция
- Формальные силлогизмы (Аристотель, логика предикатов).
- Доказательства по индукции.
- Построение и опровержение аргументов.
- API: `POST /api/v1/dialog { "input": "Все люди смертны..." }`.

### 2.5 IQ-задачи
- Матрицы Равена.
- Последовательности фигур.
- Задачи на вращение и симметрию.
- API: `POST /api/v1/dialog { "input": "Какой элемент подходит к матрице ..." }`.

### 2.6 Планирование и оптимизация
- Башни Ханоя.
- Поиск маршрутов (доставка, коммивояжёр для малых n).
- Ресурсное планирование (распределение задач).
- API: `POST /api/v1/dialog { "input": "Перенеси 4 диска в Ханое" }`.

### 2.7 Причинно-следственные связи
- Анализ «если → то» в текстах.
- Вывод причин аварий, событий.
- Построение цепочек рассуждений и доказательство.
- API: `POST /api/v1/dialog { "input": "Почему перегорела лампа..." }`.

## 3. Естественные науки

### 3.1 Физика
- Механика: кинематика, динамика, статика.
- Электричество и магнетизм.
- Термодинамика и газовые законы.
- Квантовые и релятивистские оценки (уровень задачника).
- API: `POST /api/v1/dialog { "input": "Тело брошено под углом 45°..." }`.

### 3.2 Химия
- Стехиометрия, молярные массы.
- Балансировка химических реакций.
- Органика: функциональные группы, изомерия.
- Растворы и pH.
- API: `POST /api/v1/dialog { "input": "Сбалансируй реакцию Fe + O2 → Fe2O3" }`.

### 3.3 Биология
- Классификация организмов.
- Генетические задачи (скрещивания, законы Менделя).
- Клеточные процессы, ДНК, белки.
- Физиология человека.
- API: `POST /api/v1/dialog { "input": "Что такое митоз?" }`.

### 3.4 Астрономия и астрофизика
- Орбиты планет, скорости, периоды.
- Световые годы, расстояния и параллаксы.
- Звёздные величины, спектральные классы.
- Космология (расширение Вселенной, красное смещение).
- API: `POST /api/v1/dialog { "input": "Сколько длится год на Марсе?" }`.

### 3.5 География и геология
- Столицы, страны, регионы.
- Рельеф, климатические зоны.
- Геологические процессы и минералы.
- Экономическая география (ресурсы, производство).
- API: `POST /api/v1/dialog { "input": "Где находится Большой Барьерный риф?" }`.

## 4. Время и календарь

### 4.1 Календарные расчёты
- День недели по дате (Григорианский, Юлианский).
- Праздники и перемещаемые даты.
- Фазы Луны.
- API: `POST /api/v1/dialog { "input": "Какой день недели будет 9 мая 2045?" }`.

### 4.2 Часовые пояса и конвертация
- Перевод времени между TZ.
- Летнее/зимнее время.
- Координация событий в глобальной команде.
- API: `POST /api/v1/dialog { "input": "Переведи 15:30 Мск в PST" }`.

### 4.3 Расписание и планирование
- Составление учебного расписания.
- План производства/смен.
- Управление проектом (диаграммы Ганта).
- API: `POST /api/v1/dialog { "input": "Составь расписание тренировок на неделю" }`.

### 4.4 Прогнозирование событий
- Предсказание по шаблону (повторяющиеся визиты, платежи).
- Анализ сезонности.
- Оценка рисков с временными окнами.
- API: `POST /api/v1/dialog { "input": "Когда ждать следующий отчёт, если ..." }`.

## 5. Язык и лингвистика

### 5.1 Морфология
- Разбор слов на приставки, корни, суффиксы.
- Определение части речи.
- API: `POST /api/v1/dialog { "input": "Разбери слово 'переписать'" }`.

### 5.2 Синтаксис
- Построение дерева разбора.
- Определение главных и второстепенных членов.
- API: `POST /api/v1/dialog { "input": "Разбери предложение 'Kolibri учится быстро'" }`.

### 5.3 Семантика
- Определение смысла фраз и подтекста.
- Разрешение кореференций.
- Анализ тональности и целей говорящего.
- API: `POST /api/v1/dialog { "input": "Что имел в виду автор?" }`.

### 5.4 Перевод и многоязычность
- Правила и словарные соответствия.
- Машинный перевод по шаблонам.
- API: `POST /api/v1/dialog { "input": "Переведи 'цифровой мозг' на английский" }`.

### 5.5 Лексика
- Синонимы, антонимы, омонимы.
- Толкование редких слов.
- API: `POST /api/v1/dialog { "input": "Подбери синоним к 'инновация'" }`.

### 5.6 Генерация текста
- Шаблонные описания (резюме, письма).
- Креативные формы (стихи, сказки, тосты).
- Формальные документы (договора, отчёты).
- API: `POST /api/v1/dialog { "input": "Напиши четверостишие про Kolibri" }`.

### 5.7 Фонетика
- Транскрипции (МФА, русская).
- Правила ударений.
- API: `POST /api/v1/dialog { "input": "Дай транскрипцию слова 'энциклопедия'" }`.

## 6. Память и знания

### 6.1 Сохранение фактов
- Запись фактов в F-KV с версиями.
- Проверка дубликатов и конфликтов.
- API: `POST /api/v1/dialog { "input": "Запомни: Москва — столица России" }`.

### 6.2 Воспоминание
- Поиск фактов по ключам/префиксам.
- Свёртка множественных записей.
- API: `GET /api/v1/fkv/get?prefix=Россия` → `{ "values": ["Москва"] }`.

### 6.3 Связывание знаний
- Построение графов (Россия → Москва → Кремль).
- Обратный вывод (Москва → страна).
- API: `POST /api/v1/dialog { "input": "Какая достопримечательность связана со столицей России?" }`.

### 6.4 Обновление и разрешение конфликтов
- Перезапись устаревших фактов.
- Лог ведения ревизий.
- API: `POST /api/v1/dialog { "input": "Обнови факт: столица Казахстана — Астана" }`.

### 6.5 Репликация памяти
- CRDT-операции (OR-Set, LWW).
- Конфликтные слияния между узлами.
- API: `POST /api/v1/chain/submit { "program_id": "mem-sync" }`.

## 7. Диалог и общение

### 7.1 Вопрос-ответ
- Краткие фактические ответы.
- Развёрнутые объяснения.
- API: `POST /api/v1/dialog { "input": "Сколько планет в Солнечной системе?" }`.

### 7.2 Поддержка беседы
- Удержание контекста.
- Референции на прошлые фразы.
- Управление тоном (формальный/неформальный).
- API: последовательность POST-запросов с `conversation_id`.

### 7.3 Переформулировка и резюмирование
- Пересказ длинных текстов.
- Упрощение для детей.
- API: `POST /api/v1/dialog { "input": "Перескажи кратко эту статью: ..." }`.

### 7.4 Объяснение решений
- Детальные рассуждения «почему».
- Проверка на логические ошибки.
- API: `POST /api/v1/dialog { "input": "Объясни решение задачи..." }`.

### 7.5 Рекомендации
- Выбор товаров/услуг по критериям.
- Личные планы (спорт, обучение).
- API: `POST /api/v1/dialog { "input": "Подскажи ноутбук до 1000$" }`.

### 7.6 Эмпатия и эмоции
- Отвечать с поддержкой.
- Распознавать эмоциональный контекст.
- API: `POST /api/v1/dialog { "input": "Мне грустно" }` → `{ "answer": "Сочувствую..." }`.

## 8. Самообучение и эвристика

### 8.1 Исправление ошибок
- Перепроверка собственных ответов.
- Автоматическая корректировка PoE.
- API: `POST /api/v1/dialog { "input": "Проверь предыдущий ответ" }`.

### 8.2 Прогнозирование действий пользователя
- Следующий шаг по истории диалога.
- Рекомендации продолжения.
- API: `POST /api/v1/dialog { "input": "Предскажи, что я спрошу" }`.

### 8.3 Автогенерация правил
- Извлечение паттернов из примеров.
- Формирование шаблонов Δ-VM программ.
- API: `POST /api/v1/program/submit` со сгенерированными правилами → проверка `score`.

### 8.4 Построение новых формул
- Символьное манипулирование.
- Поиск инвариантов и симметрий.
- API: `POST /api/v1/dialog { "input": "Выведи формулу площади эллипса" }`.

### 8.5 Curriculum learning
- Планирование последовательностей обучения.
- Автоматическая оценка уровня пользователя.
- API: `POST /api/v1/dialog { "input": "Составь курс по линейной алгебре" }`.

### 8.6 Auto-benchmark
- Прогон встроенных тестов.
- Самооценка прогресса.
- API: `POST /api/v1/dialog { "input": "Запусти самопроверку" }` → `{ "answer": "PoE вырос до 0.82" }`.

## 9. Социальные сценарии

### 9.1 Финансы и экономика
- Расчёт кредитов, ипотек.
- Бюджетирование семей/компаний.
- Инвестиционные стратегии.
- API: `POST /api/v1/dialog { "input": "Рассчитай платёж по кредиту 1М на 5 лет" }`.

### 9.2 Юриспруденция
- Анализ прав и обязанностей.
- Шаблоны договоров, соглашений.
- Разбор кейсов по законодательству.
- API: `POST /api/v1/dialog { "input": "Какие документы нужны для регистрации ООО?" }`.

### 9.3 Медицина (не заменяет врача)
- Триаж симптомов, рекомендации по визиту к врачу.
- Расшифровка анализов (в рамках допустимых норм).
- Медицинские протоколы и памятки.
- API: `POST /api/v1/dialog { "input": "У меня температура и кашель..." }`.

### 9.4 Образование
- Объяснение школьных и вузовских тем.
- Подготовка к экзаменам.
- Проверка домашних заданий.
- API: `POST /api/v1/dialog { "input": "Объясни теорему Гаусса" }`.

### 9.5 История и культура
- Даты и события, временные линии.
- Биографии персоналий.
- Анализ культурных явлений.
- API: `POST /api/v1/dialog { "input": "Расскажи про эпоху Возрождения" }`.

### 9.6 Социальные науки
- Социология: анализ анкет, трендов.
- Психология: модели поведения (в пределах этики).
- Политология: структуры власти, выборы.
- API: `POST /api/v1/dialog { "input": "Что такое пирамида Маслоу?" }`.

## 10. Творчество и дизайн

### 10.1 Литература
- Генерация стихов, песен, слоганов.
- Построение сюжетов и персонажей.
- API: `POST /api/v1/dialog { "input": "Придумай сюжет фантастического рассказа" }`.

### 10.2 Музыка
- Создание последовательностей нот.
- Конвертация в MIDI (10-ричная нотация).
- Теория музыки (аккорды, лад).
- API: `POST /api/v1/dialog { "input": "Сгенерируй мелодию C мажор" }`.

### 10.3 Изобразительное искусство
- ASCII-арт, формульные описания.
- Генерация палитр.
- Инструкции для художника.
- API: `POST /api/v1/dialog { "input": "Опиши композицию постера" }`.

### 10.4 Идеи и инновации
- Брейншторм решений.
- Дизайн продуктов и фич.
- API: `POST /api/v1/dialog { "input": "Придумай идеи для eco-стартапа" }`.

### 10.5 Игровой дизайн
- Механики игр, баланс.
- Квесты и сюжетные ветки.
- API: `POST /api/v1/dialog { "input": "Спроектируй квест для RPG" }`.

## 11. Сетевые сценарии Kolibri Ω

### 11.1 Обмен кадрами
- HELLO/PING: проверка доступности узлов.
- PROGRAM_OFFER/BLOCK_OFFER: распространение программ и блоков.
- FKV_DELTA: синхронизация памяти.
- API: `POST /api/v1/dialog { "input": "Отправь PING соседу" }` или прямые сетевые кадры.

### 11.2 Репутация и консенсус
- Учёт полезных блоков.
- Proof-of-Use и метрики доверия.
- Голосования и санкции.
- API: `GET /api/v1/metrics` → `{ "peers": [...], "reputation": ... }`.

### 11.3 Failover и устойчивость
- Переключение на резервные узлы.
- Проверка согласованности цепочки.
- Восстановление после сбоев.
- API: `POST /api/v1/dialog { "input": "Симулируй отказ узла" }`.

## 12. Мета-сценарии и операционный интеллект

### 12.1 Рефакторинг памяти
- Удаление дубликатов.
- Сжатие и оптимизация хранения.
- API: `POST /api/v1/dialog { "input": "Очисти память от дубликатов" }`.

### 12.2 Explainable failures
- Объяснение причин ошибок.
- Логи исполнения Δ-VM.
- API: `POST /api/v1/vm/run { "program": "..." }` → `{ "error": "gas limit" , "explain": ... }`.

### 12.3 Аудит блокчейна знаний
- Проверка целостности цепочки.
- Пересчёт хэшей и PoE.
- API: `GET /api/v1/chain/audit`.

### 12.4 Репутация узлов
- Статистика по соседям.
- История взаимодействий.
- API: `GET /api/v1/metrics?section=peers`.

### 12.5 Безопасность
- Фильтрация вредных программ.
- Проверка подписей и авторизации.
- Pen-test сценарии.
- API: `POST /api/v1/program/submit` → `{ "status": "rejected", "reason": "signature invalid" }`.

### 12.6 Мониторинг и алерты
- Наблюдение за нагрузкой и памятью.
- Настройка алертов (HTTP/webhook).
- API: `GET /api/v1/health`.

## 13. Интеграции и прикладные кейсы

### 13.1 HTTP/REST интеграции
- Вызов внешних API (погода, финансы) через прокси.
- Обработка JSON/CSV.
- API: `POST /api/v1/dialog { "input": "Сделай GET https://example.com" }`.

### 13.2 Управление кластером
- Распределение задач между узлами.
- Управление нагрузкой.
- API: `POST /api/v1/dialog { "input": "Переназначь задачу на узел #3" }`.

### 13.3 Kolibri Studio
- Визуализация памяти и программ.
- Логи синтеза, блокчейна, сети.
- API: `GET /api/v1/studio/state` для фронта.

### 13.4 Экспорт данных
- Форматы CSV/JSON/PDF.
- Обмен с BI-инструментами.
- API: `GET /api/v1/export?format=csv&type=memory`.

### 13.5 Подключение внешних систем
- IoT-сенсоры, ERP, CRM.
- Автоматизация рабочего места.
- API: `POST /api/v1/dialog { "input": "Получай данные с сенсора X" }`.

## 14. Дополнительные расширения

### 14.1 Мультимодальность
- Анализ изображений в десятичной кодировке.
- Обработка аудиосигналов через спектральные коэффициенты base-10.
- API: `POST /api/v1/dialog { "input": "Расскажи, что на изображении (код 10)" }`.

### 14.2 Коллаборация людей и Kolibri Ω
- Совместное редактирование документов.
- Обратная связь в реальном времени.
- API: `POST /api/v1/dialog { "input": "Редактируем документ вместе" }`.

### 14.3 Этика и соответствие
- Проверка на предвзятость в программах.
- Соблюдение нормативов и политик.
- API: `POST /api/v1/program/submit { "bytecode": "..." }` → `{ "compliance": "ok" }`.

### 14.4 Метрики успеха
- Отслеживание PoE, MDL, времени реакции.
- Сравнение с эталонами.
- API: `GET /api/v1/metrics?section=benchmarks`.

---

### Методология тестирования
1. **Диалоговые сценарии** — через `POST /api/v1/dialog`, проверка контекста и объяснений.
2. **Программы Δ-VM** — `POST /api/v1/vm/run`, контроль газ-лимита и трассировки.
3. **Память и сеть** — `GET/POST` к `/api/v1/fkv/*`, `/api/v1/chain/*`, `/api/v1/metrics`.
4. **Фронтенд Kolibri Studio** — визуальная проверка вкладок через web UI.
5. **Auto-benchmark** — регламентированный запуск внутренних тестов.

### Градация полноты демо
- **Базовый уровень**: покрывает не менее 5 сценариев в каждой категории.
- **Продвинутый**: включает все перечисленные примеры и собственные вариации.
- **Эталонный**: дополняет каждую подкатегорию уникальными прикладными кейсами пользователей.

### Итог
Каталог содержит 200+ атомарных задач, описанных в древовидной структуре. Они охватывают фундаментальные науки, когнитивные способности, творческие и практические навыки, сетевые функции, самообучение и интеграцию Kolibri Ω с внешними системами. Файл может использоваться как чек-лист для демонстраций, автотестов и онбординга команды.

