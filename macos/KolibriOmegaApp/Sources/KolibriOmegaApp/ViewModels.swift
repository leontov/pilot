import Foundation
import Combine

@MainActor
final class DashboardViewModel: ObservableObject {
    @Published private(set) var snapshot: DashboardSnapshot?
    @Published private(set) var lastUpdated: Date?
    private let services: KolibriServices

    init(services: KolibriServices) {
        self.services = services
    }

    func load() async {
        switch await services.fetchDashboardSnapshot() {
        case .success(let snapshot):
            self.snapshot = snapshot
            self.lastUpdated = Date()
        case .failure:
            break
        }
    }
}

@MainActor
final class HealthViewModel: ObservableObject {
    @Published private(set) var health: KolibriHealth?
    @Published private(set) var statusMessage: String = ""
    private let services: KolibriServices

    init(services: KolibriServices) {
        self.services = services
    }

    func refresh() async {
        switch await services.fetchHealth() {
        case .success(let health):
            self.health = health
            statusMessage = health.latencyMillis <= 50 ? "Stable" : "Latency warning"
        case .failure:
            statusMessage = "Unable to reach node"
        }
    }
}

@MainActor
final class DialogViewModel: ObservableObject {
    @Published var prompt: String = ""
    @Published private(set) var history: [KolibriDialogResponse] = []
    @Published private(set) var isSending = false

    private let services: KolibriServices

    init(services: KolibriServices) {
        self.services = services
    }

    func send() async {
        guard !prompt.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return }
        isSending = true
        defer { isSending = false }
        switch await services.sendDialog(message: prompt) {
        case .success(let response):
            history.append(response)
            prompt = ""
        case .failure:
            break
        }
    }
}

@MainActor
final class MemoryExplorerViewModel: ObservableObject {
    @Published var prefix: String = ""
    @Published private(set) var entries: [MemoryEntry] = []

    private let services: KolibriServices

    init(services: KolibriServices) {
        self.services = services
    }

    func loadInitial() async {
        await search(prefix: "")
    }

    func search(prefix: String) async {
        switch await services.fetchMemory(prefix: prefix) {
        case .success(let entries):
            self.entries = entries
        case .failure:
            break
        }
    }
}
