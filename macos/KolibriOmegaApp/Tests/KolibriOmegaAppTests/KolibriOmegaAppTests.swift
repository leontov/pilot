import XCTest
@testable import KolibriOmegaApp

final class KolibriOmegaAppTests: XCTestCase {
    func testMockDataLoads() async throws {
        let services = KolibriServices(configuration: .preview)
        await services.prepare()
        let healthResult = await services.fetchHealth()
        switch healthResult {
        case .success(let health):
            XCTAssertEqual(health.peerCount, 3)
        case .failure:
            XCTFail("Expected mock health data")
        }
    }
}
