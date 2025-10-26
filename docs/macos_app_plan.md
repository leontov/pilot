# Kolibri Ω macOS Application Plan

## Goals
- Deliver a native macOS application that showcases the Kolibri Ω node and provides investors with an immediately runnable demo.
- Keep the binary lightweight and aligned with the decimal-first philosophy: the app is a visual companion that talks to the existing Kolibri Ω HTTP API instead of duplicating core logic.
- Ship reusable foundations so the app can evolve alongside future roadmap milestones (networked intelligence, synthesis, blockchain explorer).

## User Personas
1. **Investor / Executive demo** – wants to boot the node and see a guided tour of the architecture and KPIs.
2. **Developer** – needs quick access to health metrics, VM traces, and F-KV snapshots while hacking on the core.
3. **Field engineer** – deploys a node on-site and requires diagnostics (uptime, peer status, block propagation).

## Release Scope (Sprint A)
1. **Launcher dashboard**
   - Start/stop local Kolibri node via `kolibri.sh` if available.
   - Display connection status to `http://localhost:9000` with retry/backoff.
   - Provide quick links to docs (`docs/readme_pro.md`, `docs/architecture.md`).
2. **Health monitor**
   - Fetch `/api/v1/health` and present uptime, memory usage, peer count, block height.
   - Highlight out-of-threshold values (e.g., latency > 50 ms).
3. **Dialog console**
   - Lightweight chat window connected to `/api/v1/dialog`.
   - Show Δ-VM traces inline with syntax highlighting for decimal opcodes.
4. **Memory explorer**
   - Prefix search UI for F-KV via `/api/v1/fkv/get`.
   - Display semantic vs. episodic entries with badges.
5. **Offline mode**
   - Graceful messaging when the node is not available.
   - Demo dataset bundled for investors (static JSON with sample chat, F-KV entries, and block headers).

## Architecture Overview
- **UI Layer** – SwiftUI scenes for Dashboard, Health, Dialog, Memory, Settings.
- **Services** – Combine publishers wrapping Kolibri Ω HTTP API.
- **State Management** – Observable view models with dependency injection via environment.
- **Persistence** – AppStorage for lightweight preferences (API endpoint override, auto-launch node).
- **Packaging** – Swift Package executable target that compiles into a `.app` via `swift build --configuration release`. A `Makefile` helper wraps codesign/notarization hooks for future automation.

## Milestone Backlog
| Milestone | Deliverables |
|-----------|--------------|
| Sprint A  | MVP scenes (Dashboard, Health, Dialog, Memory), HTTP client, mock data providers, release automation script. |
| Sprint B  | Block explorer, peer map, integration with synthesis logs, timeline view of PoE/MDL metrics. |
| Sprint C  | Offline-first caching, delta sync viewer, investor walkthrough mode with scripted prompts. |

## Risks & Mitigations
- **HTTP schema drift** – mirror server DTOs in shared models; add integration tests hitting a live Kolibri node during CI.
- **macOS-specific APIs** – isolate AppKit-only calls so the core logic stays portable to future iPadOS client.
- **Build tooling** – document Xcode and Swift toolchain requirements; provide `swift build` fallback for CI.

## Next Steps
1. Implement MVP SwiftUI app skeleton (see `macos/KolibriOmegaApp`).
2. Hook the app into CI via GitHub Actions matrix with `macos-13` runner (future work).
3. Extend docs with investor-focused walkthrough leveraging the new macOS client.
