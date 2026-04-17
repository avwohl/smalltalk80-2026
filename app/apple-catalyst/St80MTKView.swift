// st80-2026 — St80MTKView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// MTKView subclass that forwards UIKit events to the VM through
// Bridge.h. On Mac Catalyst, `UITouch.type == .indirectPointer` is
// a mouse; `UIEvent.buttonMask` discriminates left / right. On iOS,
// touches map to the red (primary) button; a long-press gesture
// routes to yellow.
//
// Key handling uses `pressesBegan` which gives us `UIKey` with
// characters and modifier flags — works for Catalyst trackpad
// keyboards and iPad external keyboards. Soft-keyboard key input
// (on iPhone/iPad) is a Phase-3 polish item.

import UIKit
import MetalKit

final class St80MTKView: MTKView {

    // MARK: - First-responder handling

    override var canBecomeFirstResponder: Bool { true }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        becomeFirstResponder()
    }

    // MARK: - Coordinate mapping

    private func vmCoords(_ pt: CGPoint) -> (Int32, Int32)? {
        let vmW = st80_display_width()
        let vmH = st80_display_height()
        guard vmW > 0, vmH > 0, bounds.width > 0, bounds.height > 0 else {
            return nil
        }
        // UIKit's coordinate system already has Y-down, so no flip.
        let sx = pt.x / bounds.width * CGFloat(vmW)
        let sy = pt.y / bounds.height * CGFloat(vmH)
        let x = Int32(sx.clamped(0, CGFloat(vmW - 1)))
        let y = Int32(sy.clamped(0, CGFloat(vmH - 1)))
        return (x, y)
    }

    // MARK: - Touch / mouse events

    private static func button(for touch: UITouch,
                               event: UIEvent?) -> St80MouseButton {
#if targetEnvironment(macCatalyst)
        // On Catalyst (and iOS 13.4+ with a mouse), UIEvent.buttonMask
        // tells us which physical button was pressed.
        if let mask = event?.buttonMask {
            if mask.contains(.secondary) { return ST80_BTN_YELLOW }
            if mask.contains(.button(3))  { return ST80_BTN_BLUE }
        }
        // Modifier remap for a single-button pointer.
        if let mods = event?.modifierFlags {
            if mods.contains(.command)   { return ST80_BTN_BLUE }
            if mods.contains(.alternate) { return ST80_BTN_YELLOW }
        }
#else
        _ = touch; _ = event
#endif
        return ST80_BTN_RED
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = touches.first,
              let (x, y) = vmCoords(t.location(in: self)) else { return }
        let b = Self.button(for: t, event: event)
        st80_post_mouse_down(x, y, b)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = touches.first,
              let (x, y) = vmCoords(t.location(in: self)) else { return }
        st80_post_mouse_move(x, y)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = touches.first,
              let (x, y) = vmCoords(t.location(in: self)) else { return }
        let b = Self.button(for: t, event: event)
        st80_post_mouse_up(x, y, b)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        touchesEnded(touches, with: event)
    }

    // MARK: - Keyboard

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        for press in presses {
            guard let key = press.key,
                  let chars = key.charactersIgnoringModifiers.unicodeScalars.first
                      ?? key.characters.unicodeScalars.first
            else { continue }
            var code = Int32(chars.value)
            if code < 0 || code > 0x7F {
                continue
            }
            switch chars.value {
            case 0x7F: code = 8  // Delete → BS
            case 0x0A: code = 13 // LF → CR
            default: break
            }
            var mods: UInt32 = 0
            let flags = key.modifierFlags
            if flags.contains(.shift)       { mods |= ST80_MOD_SHIFT }
            if flags.contains(.control)     { mods |= ST80_MOD_CTRL }
            if flags.contains(.alternate)   { mods |= ST80_MOD_OPTION }
            if flags.contains(.command)     { mods |= ST80_MOD_COMMAND }
            st80_post_key_down(code, mods)
        }
    }

    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        // Decoded keyboard — down/up pair emitted inside st80_post_key_down.
    }
}

private extension CGFloat {
    func clamped(_ lo: CGFloat, _ hi: CGFloat) -> CGFloat {
        self < lo ? lo : (self > hi ? hi : self)
    }
}
