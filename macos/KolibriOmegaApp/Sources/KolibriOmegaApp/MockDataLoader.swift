import Foundation

actor MockDataLoader {
    private(set) var health = KolibriHealth(uptimeSeconds: 0, memoryMegabytes: 0, peerCount: 0, blockHeight: 0, latencyMillis: 0)
    private(set) var dashboard = DashboardSnapshot(activePrograms: 0, proofOfEffectiveness: 0, modelDescriptionLength: 0, runtimeMillis: 0, gasUsed: 0)
    private(set) var dialog = KolibriDialogResponse(answer: "", trace: [])
    private(set) var memory: [MemoryEntry] = []

    func loadBundledData() {
        guard let url = Bundle.module.url(forResource: "MockData", withExtension: "json") else {
            return
        }
        do {
            let data = try Data(contentsOf: url)
            let payload = try JSONDecoder().decode(MockPayload.self, from: data)
            health = payload.health
            dashboard = payload.dashboard
            dialog = payload.dialog
            memory = payload.memory
        } catch {
            // Leave defaults in place when mock data cannot be loaded.
        }
    }
}

private struct MockPayload: Codable {
    let health: KolibriHealth
    let dashboard: DashboardSnapshot
    let dialog: KolibriDialogResponse
    let memory: [MemoryEntry]
}
