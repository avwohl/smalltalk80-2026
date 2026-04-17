// st80-2026 — MetalView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// UIViewRepresentable for our St80MTKView. The native macOS variant
// in ../apple/ uses NSViewRepresentable — this file is the UIKit
// counterpart.

import SwiftUI
import MetalKit
import UIKit

struct MetalView: UIViewRepresentable {
    let imagePath: String

    func makeCoordinator() -> MetalRenderer {
        MetalRenderer(imagePath: imagePath)
    }

    func makeUIView(context: Context) -> MTKView {
        guard let device = MTLCreateSystemDefaultDevice() else {
            fatalError("No Metal device available")
        }
        let view = St80MTKView(frame: .zero, device: device)
        view.colorPixelFormat = .bgra8Unorm
        view.framebufferOnly = true
        view.isPaused = false
        view.enableSetNeedsDisplay = false
        view.preferredFramesPerSecond = 60
        view.clearColor = MTLClearColor(red: 0.15, green: 0.15, blue: 0.15, alpha: 1)
        view.delegate = context.coordinator
        view.isUserInteractionEnabled = true
        context.coordinator.setup(view: view)
        return view
    }

    func updateUIView(_ uiView: MTKView, context: Context) {}
}
