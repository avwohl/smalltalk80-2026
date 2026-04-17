// st80-2026 — MetalRenderer.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Drives the VM via `st80_run(cyclesPerFrame)` inside `draw(in:)`,
// refreshes the display texture from `st80_display_pixels`, and
// blits a fullscreen quad. Shared design with the macOS renderer
// but lives in its own file to keep platform imports isolated.

import MetalKit
import Foundation

final class MetalRenderer: NSObject, MTKViewDelegate {

    private let cyclesPerFrame: Int32 = 4000
    private let imagePath: String

    private let commandQueue: MTLCommandQueue
    private var pipeline: MTLRenderPipelineState?
    private var texture: MTLTexture?
    private var textureWidth = 0
    private var textureHeight = 0
    private var vmInitialised = false
    private var vmStartFailed = false

    init(imagePath: String) {
        guard let device = MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue() else {
            fatalError("Metal unavailable")
        }
        self.commandQueue = queue
        self.imagePath = imagePath
        super.init()
    }

    func setup(view: MTKView) {
        guard let device = view.device else { return }
        pipeline = buildPipeline(device: device,
                                 colorFormat: view.colorPixelFormat)
        startVMIfNeeded()
    }

    private func buildPipeline(device: MTLDevice,
                               colorFormat: MTLPixelFormat) -> MTLRenderPipelineState? {
        guard let url = Bundle.main.url(forResource: "Shaders",
                                        withExtension: "metal"),
              let source = try? String(contentsOf: url, encoding: .utf8) else {
            st80Log("Shaders.metal missing from bundle Resources")
            return nil
        }
        let library: MTLLibrary
        do {
            library = try device.makeLibrary(source: source, options: nil)
        } catch {
            st80Log("Shader compile failed: \(error)")
            return nil
        }
        guard let vfn = library.makeFunction(name: "st80_vertex"),
              let ffn = library.makeFunction(name: "st80_fragment") else {
            st80Log("Shader functions missing after compile")
            return nil
        }
        let desc = MTLRenderPipelineDescriptor()
        desc.vertexFunction = vfn
        desc.fragmentFunction = ffn
        desc.colorAttachments[0].pixelFormat = colorFormat
        do {
            return try device.makeRenderPipelineState(descriptor: desc)
        } catch {
            st80Log("Pipeline build failed: \(error)")
            return nil
        }
    }

    private func startVMIfNeeded() {
        guard !vmInitialised, !vmStartFailed else { return }
        st80Log("Loading image: \(imagePath)")
        let ok = imagePath.withCString { cstr in st80_init(cstr) }
        if ok {
            vmInitialised = true
            st80Log("VM initialised")
        } else {
            vmStartFailed = true
            st80Log("st80_init failed for \(imagePath)")
        }
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

    func draw(in view: MTKView) {
        guard vmInitialised, let pipeline = pipeline else {
            presentBlank(view: view)
            return
        }

        _ = st80_run(cyclesPerFrame)
        _ = st80_display_sync()

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

        guard let drawable = view.currentDrawable,
              let rpd = view.currentRenderPassDescriptor,
              let cmd = commandQueue.makeCommandBuffer(),
              let enc = cmd.makeRenderCommandEncoder(descriptor: rpd) else {
            return
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
