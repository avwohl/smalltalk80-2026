// st80-2026 — MetalRenderer.swift
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Owns the Metal pipeline and the texture we blit each frame. Drives
// the VM via `st80_run(cyclesPerFrame)` inside `draw(in:)`, then
// refreshes the texture from `st80_display_pixels` and dispatches a
// fullscreen-quad draw against the drawable.

import MetalKit
import Foundation

final class MetalRenderer: NSObject, MTKViewDelegate {

    /// How many interpreter cycles to advance per screen refresh. At
    /// ~60 Hz this gives the image time to make progress without
    /// starving the UI thread. Tuneable.
    private let cyclesPerFrame: Int32 = 4000

    private let commandQueue: MTLCommandQueue
    private var pipeline: MTLRenderPipelineState?
    private var texture: MTLTexture?
    private var textureWidth = 0
    private var textureHeight = 0
    private var vmInitialised = false
    private var vmStartFailed = false

    override init() {
        guard let device = MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue() else {
            fatalError("Metal unavailable")
        }
        self.commandQueue = queue
        super.init()
    }

    // MARK: - Setup

    func setup(view: MTKView) {
        guard let device = view.device else { return }
        pipeline = buildPipeline(device: device,
                                 colorFormat: view.colorPixelFormat)
        startVMIfNeeded()
    }

    private func buildPipeline(device: MTLDevice,
                               colorFormat: MTLPixelFormat) -> MTLRenderPipelineState? {
        // Runtime shader compilation from Shaders.metal in the bundle.
        // Avoids requiring the offline Metal toolchain at build time.
        guard let url = Bundle.main.url(forResource: "Shaders",
                                        withExtension: "metal"),
              let source = try? String(contentsOf: url, encoding: .utf8) else {
            st80Log("[st80] Shaders.metal missing from bundle Resources")
            return nil
        }
        let library: MTLLibrary
        do {
            library = try device.makeLibrary(source: source, options: nil)
        } catch {
            st80Log("[st80] Shader compile failed: \(error)")
            return nil
        }
        guard let vfn = library.makeFunction(name: "st80_vertex"),
              let ffn = library.makeFunction(name: "st80_fragment") else {
            st80Log("[st80] Shader functions missing after compile")
            return nil
        }
        let desc = MTLRenderPipelineDescriptor()
        desc.vertexFunction = vfn
        desc.fragmentFunction = ffn
        desc.colorAttachments[0].pixelFormat = colorFormat
        do {
            return try device.makeRenderPipelineState(descriptor: desc)
        } catch {
            st80Log("[st80] Pipeline build failed: \(error)")
            return nil
        }
    }

    // MARK: - VM lifecycle

    private func startVMIfNeeded() {
        guard !vmInitialised, !vmStartFailed else { return }
        let imagePath = Self.resolveImagePath()
        NSLog("[st80] Loading image: \(imagePath)")
        let ok = imagePath.withCString { cstr in st80_init(cstr) }
        if ok {
            vmInitialised = true
        } else {
            vmStartFailed = true
            st80Log("[st80] st80_init failed for \(imagePath)")
        }
    }

    /// Resolution order:
    /// 1. First CLI argument after the executable
    /// 2. `ST80_IMAGE` environment variable
    /// 3. `reference/xerox-image/VirtualImage` under the current dir
    private static func resolveImagePath() -> String {
        let args = CommandLine.arguments
        if args.count > 1 && !args[1].hasPrefix("-") {
            return args[1]
        }
        if let env = ProcessInfo.processInfo.environment["ST80_IMAGE"],
           !env.isEmpty {
            return env
        }
        return "reference/xerox-image/VirtualImage"
    }

    // MARK: - MTKViewDelegate

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        guard vmInitialised, let pipeline = pipeline else {
            // Still present the clear colour so the window isn't black.
            presentBlank(view: view)
            return
        }

        // Advance the VM.
        _ = st80_run(cyclesPerFrame)

        // Sync dirty display region.
        _ = st80_display_sync()

        // Image requested quit via primitive 113.
        if st80_quit_requested() != 0 {
            st80Log("primitiveQuit signalled — terminating app")
            st80_stop()
            st80_shutdown()
            NSApp.terminate(nil)
            return
        }

        // Keep the native cursor in sync with the image's current
        // `set_cursor_image` form.
        if let st80View = view as? St80MTKView {
            st80View.refreshCursorIfChanged()
        }

        // Refresh texture from the VM's RGBA8 staging buffer.
        let w = Int(st80_display_width())
        let h = Int(st80_display_height())
        if w > 0 && h > 0, let pixels = st80_display_pixels() {
            if texture == nil || textureWidth != w || textureHeight != h {
                texture = makeTexture(device: view.device!, width: w, height: h)
                textureWidth = w
                textureHeight = h
            }
            if let tex = texture {
                let region = MTLRegionMake2D(0, 0, w, h)
                tex.replace(region: region,
                            mipmapLevel: 0,
                            withBytes: UnsafeRawPointer(pixels),
                            bytesPerRow: w * 4)
            }
        }

        // Render the texture to the drawable.
        guard let drawable = view.currentDrawable,
              let rpd = view.currentRenderPassDescriptor,
              let cmd = commandQueue.makeCommandBuffer(),
              let enc = cmd.makeRenderCommandEncoder(descriptor: rpd) else {
            return
        }

        // Letterbox to preserve the VM display's aspect ratio.
        let drawableW = Double(view.drawableSize.width)
        let drawableH = Double(view.drawableSize.height)
        let vmW = Double(textureWidth)
        let vmH = Double(textureHeight)
        if drawableW > 0 && drawableH > 0 && vmW > 0 && vmH > 0 {
            let scale = min(drawableW / vmW, drawableH / vmH)
            let vpW = vmW * scale
            let vpH = vmH * scale
            enc.setViewport(MTLViewport(
                originX: (drawableW - vpW) * 0.5,
                originY: (drawableH - vpH) * 0.5,
                width: vpW, height: vpH,
                znear: 0.0, zfar: 1.0))
        }

        enc.setRenderPipelineState(pipeline)
        if let tex = texture {
            enc.setFragmentTexture(tex, index: 0)
            enc.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
        }
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
    }

    private func makeTexture(device: MTLDevice,
                             width: Int, height: Int) -> MTLTexture? {
        let td = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm,
            width: width, height: height,
            mipmapped: false)
        td.usage = [.shaderRead]
        return device.makeTexture(descriptor: td)
    }

    private func presentBlank(view: MTKView) {
        guard let drawable = view.currentDrawable,
              let rpd = view.currentRenderPassDescriptor,
              let cmd = commandQueue.makeCommandBuffer(),
              let enc = cmd.makeRenderCommandEncoder(descriptor: rpd) else {
            return
        }
        enc.endEncoding()
        cmd.present(drawable)
        cmd.commit()
    }
}
