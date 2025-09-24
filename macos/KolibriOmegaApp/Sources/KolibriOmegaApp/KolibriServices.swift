import Foundation
import Combine

actor KolibriServices {
    private(set) var configuration: AppConfiguration
    private let session: URLSession
    private let decoder: JSONDecoder
    private let mockDataLoader: MockDataLoader

    init(configuration: AppConfiguration = AppConfiguration(),
         session: URLSession = .shared,
         decoder: JSONDecoder = JSONDecoder()) {
        self.configuration = configuration
        self.session = session
        self.decoder = decoder
        self.mockDataLoader = MockDataLoader()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
    }

    func prepare() async {
        await mockDataLoader.loadBundledData()
    }

    func fetchHealth() async -> Result<KolibriHealth, Error> {
        await request(path: "/api/v1/health", fallback: mockDataLoader.health)
    }

    func fetchDashboardSnapshot() async -> Result<DashboardSnapshot, Error> {
        await request(path: "/api/v1/metrics", fallback: mockDataLoader.dashboard)
    }

    func sendDialog(message: String) async -> Result<KolibriDialogResponse, Error> {
        let payload = KolibriDialogRequest(message: message)
        return await request(path: "/api/v1/dialog", method: "POST", payload: payload, fallback: mockDataLoader.dialog)
    }

    func fetchMemory(prefix: String) async -> Result<[MemoryEntry], Error> {
        await request(path: "/api/v1/fkv/get?prefix=\(prefix)", fallback: mockDataLoader.memory)
    }

    private func request<T: Decodable, P: Encodable>(path: String,
                                                     method: String = "GET",
                                                     payload: P? = nil,
                                                     fallback: T) async -> Result<T, Error> {
        let url = configuration.apiBaseURL.appendingPathComponent(path)
        var request = URLRequest(url: url)
        request.httpMethod = method
        if let payload {
            request.httpBody = try? JSONEncoder().encode(payload)
            request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }

        do {
            let (data, response) = try await session.data(for: request)
            guard let http = response as? HTTPURLResponse, 200..<300 ~= http.statusCode else {
                throw URLError(.badServerResponse)
            }
            let decoded = try decoder.decode(T.self, from: data)
            return .success(decoded)
        } catch {
            return .success(fallback)
        }
    }
}

private extension URL {
    func appendingPathComponent(_ component: String) -> URL {
        if component.hasPrefix("/") {
            return self.appendingPathComponent(String(component.dropFirst()))
        }
        return self.appendingPathComponent(component)
    }
}
