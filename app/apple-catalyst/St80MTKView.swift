// st80-2026 — St80MTKView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// MTKView subclass that forwards UIKit events to the VM through
// Bridge.h.
//
// Mouse buttons on Catalyst:
//   - Primary (red):    touchesBegan/Moved/Ended — default path.
//   - Secondary (yellow): UIKit swallows right-click as a context-menu
//       request before gesture recognizers / touchesBegan can see it,
//       so we register a UIContextMenuInteraction whose delegate
//       returns nil (suppressing the system menu) and synthesizes a
//       yellow-button down at the click location.
//   - Tertiary (blue): physical middle-button click. UIKit does NOT
//       deliver middle-button clicks to `touchesBegan` (tested — no
//       event arrives at all), so we bridge to AppKit at runtime and
//       install an `NSEvent` local monitor for `.otherMouseDown`.
//       Catalyst apps run on top of AppKit, so the NSEvent path is
//       live even though the SDK doesn't expose it. The NSEvent class
//       and selector are resolved via the Objective-C runtime to
//       avoid a hard link against AppKit.
//   - Ctrl+primary, ⌥+primary: yellow fallback for single-button pointers.
//
// Sticky menus. A real 3-button mouse opens a Smalltalk-80 yellow /
// blue menu by press-and-drag — the user holds the button, drags to
// the item they want, then releases. On Catalyst a click is a single
// event (no drag), so releasing immediately at the same point would
// select whatever item happens to be under the cursor (usually the
// first one). We avoid that by keeping the menu "sticky": we send
// the button-down but not the up. A UIHoverGestureRecognizer tracks
// cursor movement with no physical button held so the menu highlight
// follows the pointer. The user's next click — left, right, or middle
// — fires the pending button-up at that location, which selects the
// item under the cursor (or dismisses the menu if the click landed
// outside it). The same state machine handles both yellow and blue
// menus.
//
// When UIContextMenuInteraction swallows the right-click, UIKit still
// fires a phantom touchesCancelled/Ended on the underlying touch.
// `suppressNextTouchCancel` / `suppressNextTouchEnd` swallow those.
//
// Key handling uses `pressesBegan` which gives us `UIKey` with
// characters and modifier flags — works for Catalyst trackpad
// keyboards and iPad external keyboards. Soft-keyboard key input
// (on iPhone/iPad) is a Phase-3 polish item.

import UIKit
import MetalKit
import ObjectiveC

final class St80MTKView: MTKView {

    // MARK: - First-responder handling

    override var canBecomeFirstResponder: Bool { true }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        becomeFirstResponder()
#if targetEnvironment(macCatalyst)
        installCatalystInteractionsIfNeeded()
#endif
    }

#if targetEnvironment(macCatalyst)
    // The pending sticky menu — yellow after a right-click, blue after
    // a middle-click. nil when no menu is held open. The next click of
    // any kind fires the matching button-up at the click location to
    // select / dismiss the menu.
    fileprivate var stickyMenuButton: St80MouseButton?
    fileprivate var suppressNextTouchEnd = false
    fileprivate var suppressNextTouchCancel = false
    private var catalystInteractionsInstalled = false
    // Latest cursor position in our view's coordinate space, updated by
    // UIHoverGestureRecognizer. Used as the anchor for middle-click
    // events (which arrive through NSEvent with locations in NSWindow
    // coordinates that are awkward to convert — the hover location is
    // already in view coords and is current to within a few ms).
    fileprivate var lastHoverLocation: CGPoint = .zero
    private var nsEventMonitor: Any?

    // Custom cursor overlay. UIKit's UIPointerStyle can't carry an
    // arbitrary bitmap, so we hide the system pointer over our view
    // (`UIPointerStyle.hidden()`) and paint the Smalltalk 16×16
    // cursor form into a UIImageView that follows the hover position.
    private var cursorOverlay: UIImageView?
    private var lastCursorHash: UInt64 = 0

    private func installCatalystInteractionsIfNeeded() {
        guard !catalystInteractionsInstalled else { return }
        catalystInteractionsInstalled = true

        addInteraction(UIContextMenuInteraction(delegate: self))
        addInteraction(UIPointerInteraction(delegate: self))

        let overlay = UIImageView(
            frame: CGRect(x: 0, y: 0, width: 16, height: 16))
        overlay.isUserInteractionEnabled = false
        overlay.isHidden = true
        overlay.contentMode = .topLeft
        overlay.layer.magnificationFilter = .nearest
        addSubview(overlay)
        cursorOverlay = overlay

        // Hover tracks cursor movement with no button held — needed so
        // the Smalltalk menu highlight follows the pointer after a
        // right/middle click has opened a sticky menu, so middle-click
        // handlers know where the cursor is, and so we can move our
        // overlay cursor.
        let hover = UIHoverGestureRecognizer(
            target: self, action: #selector(handleHover(_:)))
        hover.cancelsTouchesInView = false
        addGestureRecognizer(hover)

        installMiddleButtonMonitor()
    }

    @objc private func handleHover(_ g: UIHoverGestureRecognizer) {
        let pt = g.location(in: self)
        lastHoverLocation = pt
        cursorOverlay?.frame.origin = pt
        switch g.state {
        case .began, .changed:
            cursorOverlay?.isHidden = (cursorOverlay?.image == nil)
        case .ended, .cancelled, .failed:
            cursorOverlay?.isHidden = true
        default:
            break
        }
        guard let (x, y) = vmCoords(pt) else { return }
        st80_post_mouse_move(x, y)
    }

    // Called each frame by `MetalRenderer.draw(in:)`. Polls the VM
    // for its current 16×16 cursor form and rebuilds the overlay
    // image lazily when the bits change.
    func refreshCursorIfChanged() {
        var bits = [UInt16](repeating: 0, count: 16)
        bits.withUnsafeMutableBufferPointer { buf in
            st80_cursor_image(buf.baseAddress)
        }
        var h: UInt64 = 14695981039346656037
        for w in bits { h = (h ^ UInt64(w)) &* 1099511628211 }
        guard h != lastCursorHash else { return }
        lastCursorHash = h

        let img = Self.makeCursorImage(from: bits)
        cursorOverlay?.image = img
        if img == nil { cursorOverlay?.isHidden = true }
    }

    private static func makeCursorImage(from bits: [UInt16]) -> UIImage? {
        // All-zero form means the image hasn't set a cursor yet; fall
        // back to the system arrow by returning nil (overlay stays
        // hidden, UIPointerStyle.hidden() also returns nil below).
        if bits.allSatisfy({ $0 == 0 }) { return nil }
        let size = CGSize(width: 16, height: 16)
        let fmt = UIGraphicsImageRendererFormat()
        fmt.scale = 1  // 1-to-1 with the Smalltalk bitmap, no smoothing
        fmt.opaque = false
        let renderer = UIGraphicsImageRenderer(size: size, format: fmt)
        return renderer.image { ctx in
            let cg = ctx.cgContext
            cg.setFillColor(UIColor.black.cgColor)
            for y in 0..<16 {
                let word = bits[y]
                for x in 0..<16 {
                    if (word >> (15 - x)) & 1 == 1 {
                        cg.fill(CGRect(x: x, y: y, width: 1, height: 1))
                    }
                }
            }
        }
    }

    // AppKit NSEvent bridge — Catalyst-only middle-button plumbing.
    // We use the Objective-C runtime to call
    // `+[NSEvent addLocalMonitorForEventsMatchingMask:handler:]`
    // without a static AppKit dependency. Mask bits:
    //   NSEventMaskOtherMouseDown = 1 << 25
    //   NSEventMaskOtherMouseUp   = 1 << 26
    // NSEvent types:
    //   NSEventTypeOtherMouseDown = 25
    //   NSEventTypeOtherMouseUp   = 26
    // NSEvent.buttonNumber == 2 is the middle button.
    private func installMiddleButtonMonitor() {
        guard nsEventMonitor == nil else { return }
        guard let cls: AnyClass = NSClassFromString("NSEvent") else {
            st80Log("NSEvent unavailable — physical middle-click disabled")
            return
        }
        let selector = NSSelectorFromString(
            "addLocalMonitorForEventsMatchingMask:handler:")
        guard let metaCls: AnyClass = object_getClass(cls),
              let method = class_getInstanceMethod(metaCls, selector) else {
            st80Log("NSEvent addLocalMonitor selector missing")
            return
        }

        let handler: @convention(block) (AnyObject) -> AnyObject? = {
            [weak self] event in
            guard let self = self else { return event }
            let button = (event as AnyObject).value(forKey: "buttonNumber")
                as? Int ?? -1
            if button != 2 { return event }
            let rawType = (event as AnyObject).value(forKey: "type")
                as? Int ?? 0
            let isDown = (rawType == 25)  // NSEventTypeOtherMouseDown
            let isUp   = (rawType == 26)
            guard isDown || isUp else { return event }
            DispatchQueue.main.async {
                self.handleMiddleButton(down: isDown)
            }
            return nil  // swallow so AppKit doesn't forward it anywhere else
        }

        typealias AddMonitor = @convention(c)
            (AnyClass, Selector, UInt64, AnyObject) -> AnyObject?
        let imp = method_getImplementation(method)
        let addMonitor = unsafeBitCast(imp, to: AddMonitor.self)
        let mask: UInt64 = (1 << 25) | (1 << 26)
        nsEventMonitor = addMonitor(cls, selector, mask, handler as AnyObject)
        if nsEventMonitor == nil {
            st80Log("NSEvent addLocalMonitor returned nil")
        }
    }

    private func handleMiddleButton(down: Bool) {
        guard window != nil else { return }
        guard let (x, y) = vmCoords(lastHoverLocation) else { return }

        if down {
            // If a yellow menu was already sticky, commit it first so
            // the VM's state stays consistent.
            if let pending = stickyMenuButton, pending != ST80_BTN_BLUE {
                st80_post_mouse_up(x, y, pending)
            }
            st80_post_mouse_move(x, y)
            st80_post_mouse_down(x, y, ST80_BTN_BLUE)
            stickyMenuButton = ST80_BTN_BLUE
        }
        // Ignore the up event — the blue menu stays sticky until the
        // user's next click commits it.
    }
#endif

    // MARK: - Coordinate mapping

    // Rectangle (in view-bounds coordinates) within which the VM's
    // fixed-size display is rendered. The renderer letterboxes to
    // preserve the 1-bit image's aspect ratio; hit-testing uses the
    // same rect so clicks map to the pixel the user actually sees.
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

    private func vmCoords(_ pt: CGPoint) -> (Int32, Int32)? {
        let rect = vmContentRect()
        guard rect.width > 0, rect.height > 0 else { return nil }
        let vmW = st80_display_width()
        let vmH = st80_display_height()
        // UIKit's coordinate system already has Y-down, so no flip.
        let sx = (pt.x - rect.minX) / rect.width * CGFloat(vmW)
        let sy = (pt.y - rect.minY) / rect.height * CGFloat(vmH)
        let x = Int32(sx.clamped(0, CGFloat(vmW - 1)))
        let y = Int32(sy.clamped(0, CGFloat(vmH - 1)))
        return (x, y)
    }

    // MARK: - Touch / mouse events

    private var currentButton: St80MouseButton = ST80_BTN_RED

    private static func button(for touch: UITouch,
                               event: UIEvent?) -> St80MouseButton {
#if targetEnvironment(macCatalyst)
        // Defensive fallback only — secondary/tertiary rarely show up
        // here on Catalyst. Right-click is caught by
        // UIContextMenuInteraction, middle-click by the NSEvent
        // monitor. These branches are a no-op on a proper Mac 3-button
        // mouse but matter on iPad trackpads.
        if let mask = event?.buttonMask {
            if mask.rawValue & 0x4 != 0 { return ST80_BTN_BLUE }
            if mask.contains(.secondary) { return ST80_BTN_YELLOW }
        }
        if let mods = event?.modifierFlags {
            if mods.contains(.control)   { return ST80_BTN_YELLOW }
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

#if targetEnvironment(macCatalyst)
        // A sticky menu is waiting for a commit. Fire the pending up
        // here so the menu selects / dismisses at the click location.
        // Swallow this touch's later end so no phantom red release
        // lands on top.
        if let pending = stickyMenuButton {
            st80_post_mouse_move(x, y)
            st80_post_mouse_up(x, y, pending)
            stickyMenuButton = nil
            currentButton = ST80_BTN_RED
            suppressNextTouchEnd = true
            suppressNextTouchCancel = true
            return
        }
#endif

        let b = Self.button(for: t, event: event)
        currentButton = b
        st80_post_mouse_down(x, y, b)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = touches.first,
              let (x, y) = vmCoords(t.location(in: self)) else { return }
        st80_post_mouse_move(x, y)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
#if targetEnvironment(macCatalyst)
        if suppressNextTouchEnd {
            suppressNextTouchEnd = false
            return
        }
#endif
        guard let t = touches.first,
              let (x, y) = vmCoords(t.location(in: self)) else { return }
        st80_post_mouse_up(x, y, currentButton)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
#if targetEnvironment(macCatalyst)
        if suppressNextTouchCancel {
            suppressNextTouchCancel = false
            return
        }
#endif
        guard let t = touches.first,
              let (x, y) = vmCoords(t.location(in: self)) else { return }
        st80_post_mouse_up(x, y, currentButton)
    }

    // MARK: - Keyboard

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        for press in presses {
            guard let key = press.key else { continue }

#if targetEnvironment(macCatalyst)
            // ⌘-V pastes the system clipboard into the VM as a stream
            // of key events. The image's own cut/copy/paste remains
            // unchanged; this is a one-way bridge from the Mac
            // clipboard into whatever text editor has the Smalltalk
            // caret. Copy-out would need a HAL primitive + image
            // modification and is not implemented.
            if key.modifierFlags.contains(.command),
               (key.charactersIgnoringModifiers == "v"
                || key.charactersIgnoringModifiers == "V") {
                pasteFromSystemClipboard()
                continue
            }
#endif

            guard let chars = key.charactersIgnoringModifiers.unicodeScalars.first
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

#if targetEnvironment(macCatalyst)
    private func pasteFromSystemClipboard() {
        guard let text = UIPasteboard.general.string, !text.isEmpty else { return }
        for scalar in text.unicodeScalars {
            var code = Int32(scalar.value)
            switch scalar.value {
            case 0x0A: code = 13   // LF → CR
            case 0x09: break       // leave TAB as-is
            default:
                // Clip to printable 7-bit ASCII; everything else
                // would confuse the Blue Book keyboard decoder.
                if code < 0x20 || code > 0x7E { continue }
            }
            st80_post_key_down(code, 0)
        }
    }
#endif

    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        // Decoded keyboard — down/up pair emitted inside st80_post_key_down.
    }
}

#if targetEnvironment(macCatalyst)
extension St80MTKView: UIPointerInteractionDelegate {
    // Hide the system pointer over our view so the cursor overlay we
    // paint ourselves is the only pointer visible. Returning nil here
    // also results in the default pointer, so we must explicitly
    // return `.hidden()`.
    func pointerInteraction(_ interaction: UIPointerInteraction,
                            styleFor region: UIPointerRegion) -> UIPointerStyle? {
        return .hidden()
    }
}

extension St80MTKView: UIContextMenuInteractionDelegate {
    // Catalyst delivers right-click here. Synthesize a yellow-button
    // down at the click location and mark the menu as sticky. Return
    // nil so no system menu appears. The user's next click fires the
    // matching yellow-up at that location to commit the selection.
    func contextMenuInteraction(
        _ interaction: UIContextMenuInteraction,
        configurationForMenuAtLocation location: CGPoint
    ) -> UIContextMenuConfiguration? {
        guard let (x, y) = vmCoords(location) else { return nil }

        // If another menu was already sticky (e.g. blue menu open and
        // user right-clicks), commit it at the new location first.
        if let pending = stickyMenuButton, pending != ST80_BTN_YELLOW {
            st80_post_mouse_up(x, y, pending)
        }

        suppressNextTouchCancel = true
        suppressNextTouchEnd = true

        st80_post_mouse_move(x, y)
        st80_post_mouse_down(x, y, ST80_BTN_YELLOW)
        stickyMenuButton = ST80_BTN_YELLOW
        return nil
    }
}
#endif

private extension CGFloat {
    func clamped(_ lo: CGFloat, _ hi: CGFloat) -> CGFloat {
        self < lo ? lo : (self > hi ? hi : self)
    }
}
