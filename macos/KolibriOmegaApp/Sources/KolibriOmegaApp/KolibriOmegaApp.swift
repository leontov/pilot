import SwiftUI

@main
struct KolibriOmegaApp: App {
    @StateObject private var environment = AppEnvironment()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(environment)
        }
        .windowStyle(.automatic)
        .commands {
            KolibriCommands(environment: environment)
        }
    }
}
