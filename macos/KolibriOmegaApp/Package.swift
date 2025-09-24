// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "KolibriOmegaApp",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .executable(
            name: "KolibriOmegaApp",
            targets: ["KolibriOmegaApp"]
        )
    ],
    dependencies: [
        .package(url: "https://github.com/apple/swift-collections.git", from: "1.0.4")
    ],
    targets: [
        .executableTarget(
            name: "KolibriOmegaApp",
            dependencies: [
                .product(name: "Collections", package: "swift-collections")
            ],
            resources: [
                .process("Resources")
            ]
        ),
        .testTarget(
            name: "KolibriOmegaAppTests",
            dependencies: ["KolibriOmegaApp"]
        )
    ]
)
