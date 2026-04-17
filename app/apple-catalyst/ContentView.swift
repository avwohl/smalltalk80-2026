// st80-2026 — ContentView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Top-level screen switcher: show the image library until the user
// launches an image, then hand over to the Metal-backed VM view.

import SwiftUI

struct ContentView: View {

    // Remember the last-launched image path so the app can skip the
    // library view on next launch. We store a path rather than a UUID
    // so that even if the catalog was rewritten, the app still
    // finds its last image as long as the file exists on disk.
    private static let lastImagePathKey = "st80.lastImagePath"

    @StateObject private var manager = ImageManager.shared
    @State private var launchedImagePath: String? =
        UserDefaults.standard.string(forKey: lastImagePathKey)
            .flatMap { FileManager.default.fileExists(atPath: $0) ? $0 : nil }
    @State private var showingAbout = false
    @State private var showingExport = false

    var body: some View {
        Group {
            if let path = launchedImagePath {
                // Don't extend under the Mac title bar — clicks on the
                // top strip were being interpreted as "drag the window"
                // before reaching the MTKView. Still extend to the
                // other edges for maximum VM canvas.
                #if targetEnvironment(macCatalyst)
                MetalView(imagePath: path)
                    .ignoresSafeArea(.container, edges: [.leading, .trailing, .bottom])
                #else
                HStack(spacing: 0) {
                    ControlStripView()
                    MetalView(imagePath: path)
                }
                .ignoresSafeArea(.container, edges: .bottom)
                #endif
            } else {
                ImageLibraryView(manager: manager) { image in
                    launchedImagePath = image.imagePath
                    manager.selectedImageID = image.id
                    UserDefaults.standard.set(image.imagePath,
                                              forKey: Self.lastImagePathKey)
                }
            }
        }
        .sheet(isPresented: $showingAbout) { AboutView() }
        .sheet(isPresented: $showingExport) {
            if let path = launchedImagePath {
                DocumentExporter(url: URL(fileURLWithPath: path)) {
                    showingExport = false
                }
            }
        }
        .onReceive(NotificationCenter.default.publisher(for: .st80ShowAbout)) { _ in
            showingAbout = true
        }
        .onReceive(NotificationCenter.default.publisher(for: .st80ExportImage)) { _ in
            if launchedImagePath != nil { showingExport = true }
        }
    }
}
