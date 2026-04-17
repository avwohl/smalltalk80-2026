// st80-2026 — MetalView.swift
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// SwiftUI host for an MTKView. Each draw call drives N VM cycles,
// pulls the dirty rect from the core, and uploads into a Metal
// texture that a fullscreen-quad shader blits to the drawable.

import SwiftUI
import MetalKit
import AppKit

struct MetalView: NSViewRepresentable {
    func makeCoordinator() -> MetalRenderer {
        MetalRenderer()
    }

    func makeNSView(context: Context) -> MTKView {
        st80Log("MetalView.makeNSView")
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
        context.coordinator.setup(view: view)
        return view
    }

    func updateNSView(_ nsView: MTKView, context: Context) {}
}
