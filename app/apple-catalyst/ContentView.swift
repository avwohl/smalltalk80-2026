// st80-2026 — ContentView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Top-level screen switcher:
//
//   1. AutoLaunchSplashView — shown briefly if the user has starred an
//      image and the catalog still contains it. 3-second countdown
//      with a "Show Library" escape hatch.
//   2. ImageLibraryView    — the picker, when no image is starred or
//      the user cancelled the splash.
//   3. MetalView           — once the user launches an image.

import SwiftUI

private enum Screen {
    case splash(St80Image)
    case library
    case running(String)
}

struct ContentView: View {

    @StateObject private var manager = ImageManager.shared
    @State private var screen: Screen = .library
    @State private var didApplyAutoLaunch = false
    @State private var showingAbout = false
    @State private var showingExport = false

    @AppStorage("st80.autoLaunchImageID") private var autoLaunchImageID: String?

    var body: some View {
        Group {
            switch screen {
            case .splash(let image):
                AutoLaunchSplashView(
                    imageName: image.name,
                    onLaunch: {
                        launch(image)
                    },
                    onCancel: {
                        screen = .library
                    })

            case .library:
                ImageLibraryView(manager: manager) { image in
                    launch(image)
                }

            case .running(let path):
                #if targetEnvironment(macCatalyst)
                MetalView(imagePath: path)
                    .ignoresSafeArea(.container,
                                     edges: [.leading, .trailing, .bottom])
                #else
                HStack(spacing: 0) {
                    ControlStripView()
                    MetalView(imagePath: path)
                }
                .ignoresSafeArea(.container, edges: .bottom)
                #endif
            }
        }
        .onAppear { applyAutoLaunchOnce() }
        .sheet(isPresented: $showingAbout) { AboutView() }
        .sheet(isPresented: $showingExport) {
            if case .running(let path) = screen {
                DocumentExporter(url: URL(fileURLWithPath: path)) {
                    showingExport = false
                }
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: .st80ShowAbout)) { _ in
            showingAbout = true
        }
        .onReceive(NotificationCenter.default.publisher(for: .st80ExportImage)) { _ in
            if case .running = screen { showingExport = true }
        }
    }

    private func applyAutoLaunchOnce() {
        guard !didApplyAutoLaunch else { return }
        didApplyAutoLaunch = true
        manager.load()
        guard let id = autoLaunchImageID,
              let uuid = UUID(uuidString: id),
              let image = manager.images.first(where: { $0.id == uuid }),
              image.exists else {
            screen = .library
            return
        }
        screen = .splash(image)
    }

    private func launch(_ image: St80Image) {
        manager.markLaunched(image)
        screen = .running(image.imagePath)
    }
}
