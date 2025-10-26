import Foundation

enum SidebarDestination: String, CaseIterable, Identifiable {
    case dashboard
    case health
    case dialog
    case memory
    case docs
    case about

    var id: String { rawValue }

    var title: String {
        switch self {
        case .dashboard:
            return "Dashboard"
        case .health:
            return "Health"
        case .dialog:
            return "Dialog"
        case .memory:
            return "Memory"
        case .docs:
            return "Documentation"
        case .about:
            return "About"
        }
    }

    var sfSymbol: String {
        switch self {
        case .dashboard:
            return "speedometer"
        case .health:
            return "waveform.path.ecg"
        case .dialog:
            return "bubble.left.and.bubble.right"
        case .memory:
            return "tray.full"
        case .docs:
            return "book"
        case .about:
            return "info.circle"
        }
    }

    static var monitoringCases: [SidebarDestination] { [.dashboard, .health] }
    static var knowledgeCases: [SidebarDestination] { [.dialog, .memory] }
    static var resourcesCases: [SidebarDestination] { [.docs, .about] }
}
