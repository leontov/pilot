import Foundation

struct KolibriHealth: Codable, Hashable {
    var uptimeSeconds: Int
    var memoryMegabytes: Double
    var peerCount: Int
    var blockHeight: Int
    var latencyMillis: Int
}

struct DashboardSnapshot: Codable, Hashable {
    var activePrograms: Int
    var proofOfEffectiveness: Double
    var modelDescriptionLength: Double
    var runtimeMillis: Int
    var gasUsed: Int
}

struct KolibriDialogRequest: Codable {
    var message: String
}

struct KolibriDialogResponse: Codable, Hashable {
    var answer: String
    var trace: [TraceStep]
}

struct TraceStep: Codable, Hashable, Identifiable {
    var id: UUID
    var opcode: String
    var stack: [String]
    var gasRemaining: Int
    var description: String

    init(id: UUID = UUID(), opcode: String, stack: [String], gasRemaining: Int, description: String) {
        self.id = id
        self.opcode = opcode
        self.stack = stack
        self.gasRemaining = gasRemaining
        self.description = description
    }
}

enum MemoryEntryKind: String, Codable {
    case episodic
    case semantic
    case procedural
}

struct MemoryEntry: Codable, Hashable, Identifiable {
    var id: UUID
    var key: String
    var value: String
    var kind: MemoryEntryKind
    var score: Double

    init(id: UUID = UUID(), key: String, value: String, kind: MemoryEntryKind, score: Double) {
        self.id = id
        self.key = key
        self.value = value
        self.kind = kind
        self.score = score
    }
}
