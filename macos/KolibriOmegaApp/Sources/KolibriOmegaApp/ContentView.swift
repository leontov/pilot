import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var environment: AppEnvironment
    @State private var selectedSidebar: SidebarDestination = .dashboard

    var body: some View {
        NavigationSplitView(sidebar: {
            sidebar
                .frame(minWidth: 220)
        }, detail: {
            detail
                .frame(minWidth: 540)
        })
        .task {
            await environment.bootstrap()
        }
    }

    private var sidebar: some View {
        List(selection: $selectedSidebar) {
            Section("Monitor") {
                ForEach(SidebarDestination.monitoringCases) { destination in
                    Label(destination.title, systemImage: destination.sfSymbol)
                        .tag(destination)
                }
            }
            Section("Knowledge") {
                ForEach(SidebarDestination.knowledgeCases) { destination in
                    Label(destination.title, systemImage: destination.sfSymbol)
                        .tag(destination)
                }
            }
            Section("Resources") {
                ForEach(SidebarDestination.resourcesCases) { destination in
                    Label(destination.title, systemImage: destination.sfSymbol)
                        .tag(destination)
                }
            }
        }
        .listStyle(.sidebar)
    }

    @ViewBuilder
    private var detail: some View {
        switch selectedSidebar {
        case .dashboard:
            DashboardView(viewModel: environment.dashboardViewModel)
        case .health:
            HealthView(viewModel: environment.healthViewModel)
        case .dialog:
            DialogView(viewModel: environment.dialogViewModel)
        case .memory:
            MemoryExplorerView(viewModel: environment.memoryViewModel)
        case .docs:
            DocumentationView()
        case .about:
            AboutView()
        }
    }
}

#Preview {
    ContentView()
        .environmentObject(AppEnvironment.preview())
}
