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
#if !targetEnvironment(macCatalyst)
import UIKit
#endif

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

    // iPhone-only: side of the screen the ControlStrip lives on. True
    // places it against the trailing edge — the side OPPOSITE the camera
    // notch / Dynamic Island, which sits on the device's top edge.
    // Landscape-left rotates the device top to the left, so the notch
    // ends up on the screen's left; strip goes right. Landscape-right
    // is the mirror. Pattern ported from iospharo ContentView.
    @State private var stripOnRight: Bool = false

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
                    if stripOnRight {
                        MetalView(imagePath: path)
                        ControlStripView()
                    } else {
                        ControlStripView()
                        MetalView(imagePath: path)
                    }
                }
                .ignoresSafeArea(.container, edges: .bottom)
                .onAppear {
                    UIDevice.current.beginGeneratingDeviceOrientationNotifications()
                    updateStripSide()
                }
                .onReceive(NotificationCenter.default.publisher(
                    for: UIDevice.orientationDidChangeNotification)) { _ in
                    // Safe-area insets update a beat after the orientation
                    // notification fires. Wait one tick so the strip's
                    // per-corner inset math sees the new insets.
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) {
                        updateStripSide()
                    }
                }
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

    #if !targetEnvironment(macCatalyst)
    /// Put the ControlStrip on the side opposite the camera notch /
    /// Dynamic Island. UIDevice orientation tells us where the device
    /// top (camera) points:
    ///   .landscapeLeft  → top points LEFT  → strip on RIGHT
    ///   .landscapeRight → top points RIGHT → strip on LEFT
    /// Portrait / unknown / flat falls back to the interface orientation,
    /// where the sense is inverted (interface .landscapeRight = camera
    /// on LEFT). Portrait leaves the strip on the leading edge.
    private func updateStripSide() {
        let dev = UIDevice.current.orientation
        if dev == .landscapeLeft {
            stripOnRight = true
            return
        }
        if dev == .landscapeRight {
            stripOnRight = false
            return
        }
        if let scene = UIApplication.shared.connectedScenes
            .compactMap({ $0 as? UIWindowScene }).first {
            stripOnRight = scene.interfaceOrientation == .landscapeRight
        }
    }
    #endif
}
