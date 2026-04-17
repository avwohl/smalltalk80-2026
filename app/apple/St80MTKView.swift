// st80-2026 — St80MTKView.swift
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// MTKView subclass that forwards NSEvents to the VM through
// Bridge.h's st80_post_* entry points. View coordinates are mapped
// onto the VM's display coordinate system (top-left origin, Y-down).
//
// Modifier → button mapping for single-button trackpads:
//    plain click   = red (select)
//    ⌥ + click     = yellow (do-it / print-it menu)
//    ⌘ + click     = blue (frame / close)
// Multi-button mice use their native right / middle buttons too.

import AppKit
import MetalKit

final class St80MTKView: MTKView {

    // Always accept key + mouse focus.
    override var acceptsFirstResponder: Bool { true }
    override func becomeFirstResponder() -> Bool { true }

    // MARK: - Custom cursor (from Smalltalk's set_cursor_image)

    private var currentCursor: NSCursor?
    private var lastCursorHash: UInt64 = 0

    /// Called by `MetalRenderer.draw(in:)` each frame. Polls the VM
    /// for its current 16×16 cursor form; if it has changed since the
    /// last poll, rebuild the native `NSCursor` and refresh the
    /// view's cursor rect.
    func refreshCursorIfChanged() {
        var bits = [UInt16](repeating: 0, count: 16)
        bits.withUnsafeMutableBufferPointer { buf in
            st80_cursor_image(buf.baseAddress)
        }
        var h: UInt64 = 14695981039346656037  // FNV-1a offset
        for w in bits {
            h = (h ^ UInt64(w)) &* 1099511628211
        }
        guard h != lastCursorHash else { return }
        lastCursorHash = h
        currentCursor = Self.makeCursor(from: bits)
        window?.invalidateCursorRects(for: self)
    }

    override func resetCursorRects() {
        if let c = currentCursor {
            addCursorRect(bounds, cursor: c)
        } else {
            super.resetCursorRects()
        }
    }

    private static func makeCursor(from bits: [UInt16]) -> NSCursor? {
        // All-zero form means "no image yet" — fall back to default
        // arrow rather than a 16×16 hole.
        if bits.allSatisfy({ $0 == 0 }) { return NSCursor.arrow }

        guard let rep = NSBitmapImageRep(
            bitmapDataPlanes: nil,
            pixelsWide: 16, pixelsHigh: 16,
            bitsPerSample: 8, samplesPerPixel: 4,
            hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB,
            bytesPerRow: 16 * 4, bitsPerPixel: 32),
              let data = rep.bitmapData
        else { return nil }

        // Fill RGBA. Smalltalk bit 0 = MSB = leftmost pixel; set = black
        // opaque, clear = transparent. That's the classic Alto/Xerox
        // cursor convention (no separate mask).
        for y in 0..<16 {
            let word = bits[y]
            for x in 0..<16 {
                let bit = (word >> (15 - x)) & 1
                let o = (y * 16 + x) * 4
                data[o + 0] = 0
                data[o + 1] = 0
                data[o + 2] = 0
                data[o + 3] = bit == 1 ? 255 : 0
            }
        }

        let img = NSImage(size: NSSize(width: 16, height: 16))
        img.addRepresentation(rep)
        return NSCursor(image: img, hotSpot: NSPoint(x: 0, y: 0))
    }

    // MARK: - Mouse tracking

    private var trackingArea: NSTrackingArea?

    override func updateTrackingAreas() {
        if let existing = trackingArea {
            removeTrackingArea(existing)
        }
        let area = NSTrackingArea(
            rect: bounds,
            options: [.activeInKeyWindow, .mouseMoved,
                      .mouseEnteredAndExited, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(area)
        trackingArea = area
        super.updateTrackingAreas()
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        window?.makeFirstResponder(self)
    }

    // MARK: - Coordinate mapping

    // Rectangle (in view-bounds, Y-up) the VM's fixed-size display
    // actually renders into. Renderer letterboxes to preserve the
    // 1-bit display's aspect ratio; hit-testing matches.
    private func vmContentRect() -> CGRect {
        let vmW = CGFloat(st80_display_width())
        let vmH = CGFloat(st80_display_height())
        guard vmW > 0, vmH > 0, bounds.width > 0, bounds.height > 0 else {
            return bounds
        }
        let scale = min(bounds.width / vmW, bounds.height / vmH)
        let w = vmW * scale
        let h = vmH * scale
        return CGRect(x: (bounds.width - w) * 0.5,
                      y: (bounds.height - h) * 0.5,
                      width: w, height: h)
    }

    private func vmCoords(for event: NSEvent) -> (Int32, Int32)? {
        let pt = convert(event.locationInWindow, from: nil)
        let rect = vmContentRect()
        guard rect.width > 0, rect.height > 0 else { return nil }
        let vmW = st80_display_width()
        let vmH = st80_display_height()
        // NSView has Y-up; VM display has Y-down (BitBlt convention).
        let sx = (pt.x - rect.minX) / rect.width * CGFloat(vmW)
        let sy = (rect.maxY - pt.y) / rect.height * CGFloat(vmH)
        let x = Int32(sx.clamped(0, CGFloat(vmW - 1)))
        let y = Int32(sy.clamped(0, CGFloat(vmH - 1)))
        return (x, y)
    }

    private func button(for event: NSEvent) -> St80MouseButton {
        let mods = event.modifierFlags
        if mods.contains(.command) { return ST80_BTN_BLUE }
        if mods.contains(.option)  { return ST80_BTN_YELLOW }
        return ST80_BTN_RED
    }

    // MARK: - Mouse events

    override func mouseMoved(with event: NSEvent) {
        if let (x, y) = vmCoords(for: event) { st80_post_mouse_move(x, y) }
    }

    override func mouseDragged(with event: NSEvent) {
        mouseMoved(with: event)
    }
    override func rightMouseDragged(with event: NSEvent) {
        mouseMoved(with: event)
    }
    override func otherMouseDragged(with event: NSEvent) {
        mouseMoved(with: event)
    }

    override func mouseDown(with event: NSEvent) {
        guard let (x, y) = vmCoords(for: event) else { return }
        st80_post_mouse_down(x, y, button(for: event))
    }

    override func mouseUp(with event: NSEvent) {
        guard let (x, y) = vmCoords(for: event) else { return }
        st80_post_mouse_up(x, y, button(for: event))
    }

    override func rightMouseDown(with event: NSEvent) {
        guard let (x, y) = vmCoords(for: event) else { return }
        st80_post_mouse_down(x, y, ST80_BTN_YELLOW)
    }

    override func rightMouseUp(with event: NSEvent) {
        guard let (x, y) = vmCoords(for: event) else { return }
        st80_post_mouse_up(x, y, ST80_BTN_YELLOW)
    }

    override func otherMouseDown(with event: NSEvent) {
        guard let (x, y) = vmCoords(for: event) else { return }
        st80_post_mouse_down(x, y, ST80_BTN_BLUE)
    }

    override func otherMouseUp(with event: NSEvent) {
        guard let (x, y) = vmCoords(for: event) else { return }
        st80_post_mouse_up(x, y, ST80_BTN_BLUE)
    }

    // MARK: - Keyboard

    override func keyDown(with event: NSEvent) {
        // Use `characters` (shift/ctrl applied) rather than
        // `charactersIgnoringModifiers` — the image expects the
        // already-shifted ASCII character in "decoded keyboard" mode
        // (dbanay main.cpp:593). With charactersIgnoringModifiers,
        // shift-A would arrive as lowercase 'a'.
        guard let chars = event.characters,
              let scalar = chars.unicodeScalars.first else { return }
        var code = Int32(scalar.value)

        // Gate to 7-bit ASCII. NSEvent emits ≥0xF700 for function /
        // arrow / special keys; dbanay main.cpp:658 drops those too.
        if code < 0 || code > 0x7F { return }

        // Map macOS special keys onto Blue Book / ASCII codes the
        // Smalltalk image expects.
        switch scalar.value {
        case 0x7F:  // macOS Delete key (NSDeleteCharacter) → ASCII BS
            code = 8
        case 0x0A:  // LF → CR (Smalltalk uses CR as line terminator)
            code = 13
        default:
            break
        }

        var mods: UInt32 = 0
        let flags = event.modifierFlags
        if flags.contains(.shift)   { mods |= ST80_MOD_SHIFT }
        if flags.contains(.control) { mods |= ST80_MOD_CTRL }
        if flags.contains(.option)  { mods |= ST80_MOD_OPTION }
        if flags.contains(.command) { mods |= ST80_MOD_COMMAND }
        st80_post_key_down(code, mods)
    }

    override func keyUp(with event: NSEvent) {
        // Decoded keyboard: the type-3 down and type-4 up words are
        // emitted as a pair inside st80_post_key_down. A separate
        // key-up isn't forwarded to the VM.
    }
}

private extension CGFloat {
    func clamped(_ lo: CGFloat, _ hi: CGFloat) -> CGFloat {
        self < lo ? lo : (self > hi ? hi : self)
    }
}
