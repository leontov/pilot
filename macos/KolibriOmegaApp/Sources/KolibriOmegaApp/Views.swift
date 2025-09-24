import SwiftUI

struct DashboardView: View {
    @ObservedObject var viewModel: DashboardViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Kolibri Ω Launcher")
                .font(.largeTitle)
                .bold()
            Text("Monitor the decimal intelligence node, trigger dialogues, and keep an eye on proof metrics.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            if let snapshot = viewModel.snapshot {
                dashboardGrid(snapshot: snapshot)
            } else {
                ProgressView("Connecting to node…")
            }
            Spacer()
        }
        .padding()
        .task {
            await viewModel.load()
        }
    }

    private func dashboardGrid(snapshot: DashboardSnapshot) -> some View {
        Grid(alignment: .leading, horizontalSpacing: 24, verticalSpacing: 18) {
            GridRow {
                MetricTile(title: "Active programs", value: "\(snapshot.activePrograms)", trend: .neutral)
                MetricTile(title: "PoE", value: String(format: "%.2f", snapshot.proofOfEffectiveness), trend: .positive)
            }
            GridRow {
                MetricTile(title: "MDL", value: String(format: "%.1f", snapshot.modelDescriptionLength), trend: .neutral)
                MetricTile(title: "Runtime", value: "\(snapshot.runtimeMillis) ms", trend: .warning(snapshot.runtimeMillis > 50))
            }
            GridRow {
                MetricTile(title: "Gas", value: "\(snapshot.gasUsed)", trend: .neutral)
            }
        }
    }
}

struct HealthView: View {
    @ObservedObject var viewModel: HealthViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Text("Node health")
                .font(.title)
                .bold()
            if let health = viewModel.health {
                HealthSummaryView(health: health)
            } else {
                ProgressView("Fetching health data…")
            }
            Text(viewModel.statusMessage)
                .font(.callout)
                .foregroundStyle(.secondary)
            Spacer()
        }
        .padding()
        .task {
            await viewModel.refresh()
        }
    }
}

struct DialogView: View {
    @ObservedObject var viewModel: DialogViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            List(viewModel.history.indices, id: \.self) { index in
                DialogTranscriptView(response: viewModel.history[index])
            }
            .listStyle(.plain)
            HStack {
                TextField("Decimal prompt", text: $viewModel.prompt, axis: .vertical)
                Button {
                    Task { await viewModel.send() }
                } label: {
                    if viewModel.isSending {
                        ProgressView()
                    } else {
                        Label("Send", systemImage: "paperplane.fill")
                    }
                }
                .disabled(viewModel.isSending)
            }
        }
        .padding()
    }
}

struct MemoryExplorerView: View {
    @ObservedObject var viewModel: MemoryExplorerViewModel

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                TextField("Prefix", text: Binding(
                    get: { viewModel.prefix },
                    set: { prefix in
                        viewModel.prefix = prefix
                        Task { await viewModel.search(prefix: prefix) }
                    }
                ))
                .textFieldStyle(.roundedBorder)
                .frame(width: 240)
                Spacer()
            }
            List(viewModel.entries) { entry in
                MemoryRow(entry: entry)
            }
            .listStyle(.inset)
        }
        .padding()
    }
}

struct DocumentationView: View {
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Documentation quick links")
                .font(.title2)
                .bold()
            Link(destination: URL(string: "https://github.com/kolibri-ai/pilot/blob/main/docs/readme_pro.md")!) {
                Label("README Pro", systemImage: "doc.text")
            }
            Link(destination: URL(string: "https://github.com/kolibri-ai/pilot/blob/main/docs/architecture.md")!) {
                Label("Technical overview", systemImage: "building.columns")
            }
            Link(destination: URL(string: "https://github.com/kolibri-ai/pilot/blob/main/docs/demos.md")!) {
                Label("Demo playbook", systemImage: "sparkles")
            }
            Spacer()
        }
        .padding()
    }
}

struct AboutView: View {
    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("About Kolibri Ω")
                .font(.title)
                .bold()
            Text("A compact decimal-first intelligence stack capable of dialog, synthesis, and knowledge exchange without neural weights.")
            Text("macOS companion \u2013 build \"KolibriOmegaApp\" via `swift build --configuration release` on macOS 13+.")
                .font(.footnote)
                .foregroundStyle(.secondary)
            Spacer()
        }
        .padding()
    }
}

struct MetricTile: View {
    enum Trend {
        case positive
        case warning(Bool)
        case neutral

        var accent: Color {
            switch self {
            case .positive:
                return .green
            case .warning(let isWarning):
                return isWarning ? .orange : .secondary
            case .neutral:
                return .accentColor
            }
        }
    }

    let title: String
    let value: String
    let trend: Trend

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title.uppercased())
                .font(.caption)
                .foregroundStyle(.secondary)
            Text(value)
                .font(.title2)
                .bold()
            Capsule()
                .fill(trend.accent.gradient)
                .frame(height: 4)
        }
        .padding()
        .background(.thinMaterial)
        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
    }
}

struct MemoryRow: View {
    let entry: MemoryEntry

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text(entry.key)
                    .font(.headline)
                Spacer()
                Text(entry.kind.rawValue.capitalized)
                    .font(.caption)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(kindColor.opacity(0.2))
                    .foregroundStyle(kindColor)
                    .clipShape(Capsule())
            }
            Text(entry.value)
                .font(.body)
            ProgressView(value: entry.score, total: 1.0) {
                Text("Score")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .progressViewStyle(.linear)
        }
    }

    private var kindColor: Color {
        switch entry.kind {
        case .episodic: return .blue
        case .semantic: return .purple
        case .procedural: return .orange
        }
    }
}

struct HealthSummaryView: View {
    let health: KolibriHealth

    var body: some View {
        Grid(alignment: .leading, horizontalSpacing: 18, verticalSpacing: 12) {
            GridRow {
                HealthBadge(title: "Uptime", value: formatTime(health.uptimeSeconds), systemImage: "clock")
                HealthBadge(title: "Memory", value: String(format: "%.1f MB", health.memoryMegabytes), systemImage: "memorychip")
            }
            GridRow {
                HealthBadge(title: "Peers", value: "\(health.peerCount)", systemImage: "person.3")
                HealthBadge(title: "Blocks", value: "#\(health.blockHeight)", systemImage: "cube")
            }
            GridRow {
                HealthBadge(title: "Latency", value: "\(health.latencyMillis) ms", systemImage: "bolt.horizontal")
            }
        }
    }

    private func formatTime(_ seconds: Int) -> String {
        let hours = seconds / 3600
        let minutes = (seconds % 3600) / 60
        return "\(hours)h \(minutes)m"
    }
}

struct HealthBadge: View {
    let title: String
    let value: String
    let systemImage: String

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Label(title, systemImage: systemImage)
                .font(.caption)
                .foregroundStyle(.secondary)
            Text(value)
                .font(.title3)
                .bold()
        }
        .padding()
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.thinMaterial)
        .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))
    }
}

struct DialogTranscriptView: View {
    let response: KolibriDialogResponse

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(response.answer)
                .font(.headline)
            VStack(alignment: .leading, spacing: 6) {
                Text("Δ-VM Trace")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                ForEach(response.trace) { step in
                    TraceRow(step: step)
                }
            }
        }
        .padding()
        .background(RoundedRectangle(cornerRadius: 12).fill(Color(.secondarySystemBackground)))
    }
}

struct TraceRow: View {
    let step: TraceStep

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Text(step.opcode)
                .monospaced()
                .font(.body)
            Spacer()
            Text("Gas: \(step.gasRemaining)")
                .font(.caption)
                .foregroundStyle(.secondary)
        }
        Text(step.description)
            .font(.caption)
        Text(step.stack.joined(separator: ", "))
            .font(.caption2)
            .foregroundStyle(.secondary)
            .monospaced()
        Divider()
    }
}

struct KolibriCommands: Commands {
    @ObservedObject var environment: AppEnvironment

    var body: some Commands {
        CommandGroup(after: .appInfo) {
            Button("Refresh Health") {
                Task { await environment.healthViewModel.refresh() }
            }.keyboardShortcut("r", modifiers: [.command])
        }
    }
}

#Preview("Dashboard") {
    DashboardView(viewModel: DashboardViewModel(services: KolibriServices(configuration: .preview)))
}
