import Foundation
import Combine

@MainActor
final class AppEnvironment: ObservableObject {
    @Published private(set) var configuration = AppConfiguration()
    let dashboardViewModel: DashboardViewModel
    let healthViewModel: HealthViewModel
    let dialogViewModel: DialogViewModel
    let memoryViewModel: MemoryExplorerViewModel

    private let services: KolibriServices

    init(services: KolibriServices = KolibriServices()) {
        self.services = services
        self.dashboardViewModel = DashboardViewModel(services: services)
        self.healthViewModel = HealthViewModel(services: services)
        self.dialogViewModel = DialogViewModel(services: services)
        self.memoryViewModel = MemoryExplorerViewModel(services: services)
    }

    func bootstrap() async {
        await services.prepare()
        await dashboardViewModel.load()
        await healthViewModel.refresh()
        await memoryViewModel.loadInitial()
    }

    static func preview() -> AppEnvironment {
        AppEnvironment(services: KolibriServices(configuration: .preview))
    }
}

struct AppConfiguration {
    var apiBaseURL: URL = URL(string: "http://localhost:9000")!
    var kolibriScriptPath: String = "./kolibri.sh"

    static let preview = AppConfiguration(apiBaseURL: URL(string: "http://localhost:9000")!)
}
