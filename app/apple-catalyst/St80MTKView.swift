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
#if targetEnvironment(macCatalyst)
        becomeFirstResponder()
        installCatalystInteractionsIfNeeded()
#else
        // On iOS the soft keyboard pops up when this view becomes
        // first responder; the user explicitly opts in via the
        // control strip's keyboard toggle. Hardware keyboard still
        // works from the window's first responder chain.
        installIOSGestureRecognizersIfNeeded()
        St80InputController.shared.mtkView = self
#endif
    }

    // The pending sticky menu — yellow after a right-click / long-press,
    // blue after a middle-click / two-finger tap. nil when no menu is
    // held open. The next tap of any kind fires the matching button-up
    // at that location to select the highlighted item or dismiss.
    fileprivate var stickyMenuButton: St80MouseButton?
    fileprivate var suppressNextTouchEnd = false
    fileprivate var suppressNextTouchCancel = false

#if targetEnvironment(macCatalyst)
    private var catalystInteractionsInstalled = false
    // Latest cursor position in our view's coordinate space, updated by
    // UIHoverGestureRecognizer. Used as the anchor for middle-click
    // events (which arrive through NSEvent with locations in NSWindow
    // coordinates that are awkward to convert — the hover location is
    // already in view coords and is current to within a few ms).
    fileprivate var lastHoverLocation: CGPoint = .zero
    private var nsEventMonitor: Any?

    // Custom cursor overlay. UIKit's UIPointerStyle can't carry an
    // arbitrary bitmap, so when the image has set a non-zero cursor
    // form we hide the system pointer via `UIPointerStyle.hidden()`
    // and paint the Smalltalk 16×16 cursor into a UIImageView that
    // tracks the hover position. When the image hasn't set a custom
    // cursor (boot state, or cursor form is all-zero) we leave the
    // system pointer visible so the user isn't left with no cursor.
    private var cursorOverlay: UIImageView?
    private var lastCursorHash: UInt64 = 0
    private var pointerInteraction: UIPointerInteraction?

    private func installCatalystInteractionsIfNeeded() {
        guard !catalystInteractionsInstalled else { return }
        catalystInteractionsInstalled = true

        addInteraction(UIContextMenuInteraction(delegate: self))
        let pointer = UIPointerInteraction(delegate: self)
        addInteraction(pointer)
        pointerInteraction = pointer

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
        switch g.state {
        case .began, .changed:
            moveCursorOverlay(to: pt)
        case .ended, .cancelled, .failed:
            cursorOverlay?.isHidden = true
        default:
            break
        }
        guard let (x, y) = vmCoords(pt) else { return }
        st80_post_mouse_move(x, y)
    }

    // Move the overlay to `pt` and show it iff we actually have a
    // Smalltalk cursor bitmap to paint. Shared between hover and the
    // touchesBegan/Moved paths so the overlay tracks the pointer
    // during click-drag too (UIHoverGestureRecognizer stops firing
    // while any button is held).
    private func moveCursorOverlay(to pt: CGPoint) {
        guard let overlay = cursorOverlay else { return }
        overlay.frame.origin = pt
        overlay.isHidden = (overlay.image == nil)
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
        let hadImage = cursorOverlay?.image != nil
        cursorOverlay?.image = img
        if img == nil { cursorOverlay?.isHidden = true }
        // If we gained or lost a custom cursor image, tell the
        // pointer interaction to re-query our style so the system
        // pointer toggles between visible and hidden.
        if hadImage != (img != nil) {
            pointerInteraction?.invalidate()
        }
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

#if !targetEnvironment(macCatalyst)
    // MARK: - iOS gesture recognizers
    //
    // iOS (iPad / iPhone) has no middle or right mouse button. We map:
    //   - single tap / drag          → red (via touchesBegan/Moved/Ended)
    //   - long-press (0.4s, hold)    → yellow, sticky until next tap
    //   - two-finger tap             → blue,   sticky until next tap
    // The sticky state is committed inside touchesBegan above — same
    // state machine Catalyst uses for right-click / middle-click.

    private var iosGesturesInstalled = false

    private func installIOSGestureRecognizersIfNeeded() {
        guard !iosGesturesInstalled else { return }
        iosGesturesInstalled = true

        let yellow = UILongPressGestureRecognizer(
            target: self, action: #selector(handleYellowLongPress(_:)))
        yellow.minimumPressDuration = 0.4
        yellow.allowableMovement = .greatestFiniteMagnitude
        yellow.cancelsTouchesInView = true
        // Without this, touchesBegan posts a RED down before the long-press
        // recognizes — a spurious click that clears any text selection, so
        // "do it" opens on an empty selection. Delaying defers touchesBegan
        // until the gesture decides it will NOT recognize.
        yellow.delaysTouchesBegan = true
        addGestureRecognizer(yellow)

        let blue = UITapGestureRecognizer(
            target: self, action: #selector(handleBlueTwoFingerTap(_:)))
        blue.numberOfTouchesRequired = 2
        blue.cancelsTouchesInView = true
        addGestureRecognizer(blue)
    }

    @objc private func handleYellowLongPress(_ g: UILongPressGestureRecognizer) {
        guard g.state == .began else { return }
        guard let (x, y) = vmCoords(g.location(in: self)) else { return }
        // Commit any pending blue menu first at the new location, then
        // open yellow sticky. User's next tap commits yellow.
        if let pending = stickyMenuButton, pending != ST80_BTN_YELLOW {
            st80_post_mouse_up(x, y, pending)
        }
        st80_post_mouse_move(x, y)
        st80_post_mouse_down(x, y, ST80_BTN_YELLOW)
        stickyMenuButton = ST80_BTN_YELLOW
        // `cancelsTouchesInView = true` means touchesEnded for the
        // original finger won't fire, so no suppression flags needed.
    }

    @objc private func handleBlueTwoFingerTap(_ g: UITapGestureRecognizer) {
        guard g.state == .ended else { return }
        guard let (x, y) = vmCoords(g.location(in: self)) else { return }
        if let pending = stickyMenuButton, pending != ST80_BTN_BLUE {
            st80_post_mouse_up(x, y, pending)
        }
        st80_post_mouse_move(x, y)
        st80_post_mouse_down(x, y, ST80_BTN_BLUE)
        stickyMenuButton = ST80_BTN_BLUE
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
        guard let t = touches.first else { return }
        let pt = t.location(in: self)
        guard let (x, y) = vmCoords(pt) else { return }

#if !targetEnvironment(macCatalyst)
        // Keep soft-keyboard text input alive: SwiftUI Button taps on the
        // ControlStrip steal first responder. Without this, typing "stops
        // going anywhere" after the user interacts with any strip button.
        if St80InputController.shared.keyboardVisible && !isFirstResponder {
            _ = becomeFirstResponder()
        }
#endif

#if targetEnvironment(macCatalyst)
        lastHoverLocation = pt
        moveCursorOverlay(to: pt)
#endif
        // A sticky menu is waiting for a commit. Fire the pending up
        // here so the menu selects / dismisses at the click location.
        // Swallow this touch's later end so no phantom red release
        // lands on top. Shared across Catalyst and iOS — both
        // platforms' gesture paths populate `stickyMenuButton`.
        if let pending = stickyMenuButton {
            st80_post_mouse_move(x, y)
            st80_post_mouse_up(x, y, pending)
            stickyMenuButton = nil
            currentButton = ST80_BTN_RED
            suppressNextTouchEnd = true
            suppressNextTouchCancel = true
            return
        }

        let b = Self.button(for: t, event: event)
        currentButton = b
        st80_post_mouse_down(x, y, b)
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
        guard let t = touches.first else { return }
        let pt = t.location(in: self)
        guard let (x, y) = vmCoords(pt) else { return }
#if targetEnvironment(macCatalyst)
        lastHoverLocation = pt
        moveCursorOverlay(to: pt)
#endif
        st80_post_mouse_move(x, y)
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
        if suppressNextTouchEnd {
            suppressNextTouchEnd = false
            return
        }
        guard let t = touches.first,
              let (x, y) = vmCoords(t.location(in: self)) else { return }
        st80_post_mouse_up(x, y, currentButton)
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
        if suppressNextTouchCancel {
            suppressNextTouchCancel = false
            return
        }
        guard let t = touches.first,
              let (x, y) = vmCoords(t.location(in: self)) else { return }
        st80_post_mouse_up(x, y, currentButton)
    }

    // MARK: - Keyboard

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        for press in presses {
            guard let key = press.key else { continue }

            // ⌘-V / ⌘-C / ⌘-X bridge the VM and host clipboards:
            //   ⌘-V streams the host clipboard into the VM as key events
            //        (keeps the image's own Ctrl+V binding working too —
            //         the image's CurrentSelection is left untouched).
            //   ⌘-C / ⌘-X forward to the VM as Ctrl+C / Ctrl+X, then
            //        after a short delay the VM's updated
            //        ParagraphEditor>>CurrentSelection is mirrored to
            //        UIPasteboard. See `copyVMSelectionToSystemClipboard`.
            let ignoringMods = key.charactersIgnoringModifiers
            if key.modifierFlags.contains(.command) {
                if ignoringMods == "v" || ignoringMods == "V" {
                    pasteFromSystemClipboard()
                    continue
                }
                if ignoringMods == "c" || ignoringMods == "C" {
                    copyVMSelectionToSystemClipboard(cut: false)
                    continue
                }
                if ignoringMods == "x" || ignoringMods == "X" {
                    copyVMSelectionToSystemClipboard(cut: true)
                    continue
                }
            }

            // Prefer `key.characters` (fully resolved: shift and ctrl already
            // applied) over `charactersIgnoringModifiers`. UIKit delivers
            // Shift+'=' as characters="+" but charactersIgnoringModifiers="=" —
            // reading the unshifted form first sent '=' when the user typed
            // '+'. Also makes Ctrl+D arrive as 0x04 (decoded keyboard's do-it).
            guard let chars = key.characters.unicodeScalars.first
                      ?? key.charactersIgnoringModifiers.unicodeScalars.first
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
            // Cmd+letter: Cocoa doesn't fold Cmd into the resolved char
            // (unlike Ctrl, which `key.characters` already collapses to
            // 0x01..0x1F). Fold it here so Cmd+C/X/D/P/S/L/Q etc. reach
            // the Blue Book image as the matching ASCII control code.
            // Cmd+V was handled above; Cmd+Shift combos (uppercase
            // letter) fold the same way since `code & 0x1F` is
            // case-insensitive for 0x40..0x7E.
            if key.modifierFlags.contains(.command),
               code >= 0x40, code <= 0x7E {
                code &= 0x1F
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

    func pasteFromSystemClipboard() {
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

    /// Send a VM-internal cut or copy (Ctrl+X / Ctrl+C) and mirror the
    /// resulting selection to `UIPasteboard.general` once the VM has
    /// had a chance to update `ParagraphEditor class>>CurrentSelection`.
    ///
    /// The 150ms delay is a best-effort: the interpreter runs on its own
    /// worker and input events are queued, so there's no synchronous
    /// barrier we can wait on. In practice a few tens of ms is plenty;
    /// if the user hammers Cmd+C faster than that, one stale read is
    /// the worst case (the previous selection ends up on the clipboard).
    func copyVMSelectionToSystemClipboard(cut: Bool) {
        let controlByte: Int32 = cut ? 0x18 : 0x03  // Ctrl+X / Ctrl+C
        st80_post_key_down(controlByte, 0)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) {
            guard let cstr = st80_clipboard_read() else { return }
            let text = String(cString: cstr)
            guard !text.isEmpty else { return }
            UIPasteboard.general.string = text
        }
    }

    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        // Decoded keyboard — down/up pair emitted inside st80_post_key_down.
    }
}

// MARK: - UIKeyInput (iOS soft keyboard)
//
// On iOS, conforming to UIKeyInput + being first responder causes
// UIKit to pop up the soft keyboard. Each typed character arrives
// via `insertText(_:)`; backspace via `deleteBackward()`. Hardware
// keyboard input keeps working through `pressesBegan` above; we
// only take this branch for the iPad soft keyboard. Mac Catalyst
// never needs this (hardware keyboard or external).
#if !targetEnvironment(macCatalyst)
extension St80MTKView: UIKeyInput {

    var hasText: Bool { true }

    func insertText(_ text: String) {
        let ctrl = St80InputController.shared.ctrlActive
        let cmd  = St80InputController.shared.cmdActive

        // ⌘-V / ⌘-C / ⌘-X — mirror the Mac hardware-keyboard path above.
        // Paste: streams iOS pasteboard into the VM. Copy/Cut: sends the
        // matching control byte to the VM and then pushes
        // CurrentSelection up to UIPasteboard after a short delay.
        if cmd && (text == "v" || text == "V") {
            pasteFromSystemClipboard()
            St80InputController.shared.cmdActive = false
            return
        }
        if cmd && (text == "c" || text == "C") {
            copyVMSelectionToSystemClipboard(cut: false)
            St80InputController.shared.cmdActive = false
            return
        }
        if cmd && (text == "x" || text == "X") {
            copyVMSelectionToSystemClipboard(cut: true)
            St80InputController.shared.cmdActive = false
            return
        }

        for scalar in text.unicodeScalars {
            var code = Int32(scalar.value)
            switch scalar.value {
            case 0x0A: code = 13        // LF → CR
            case 0x09: break            // leave TAB as-is
            default:
                if code < 0x20 || code > 0x7E { continue }
            }
            // If the virtual Ctrl *or* Cmd toggle is armed and we're sending a
            // printable character 0x40–0x7E, fold it to the matching
            // ASCII control code (A→1, D→4, V→22, …). The Blue Book
            // decoded keyboard has no modifier side-channel, so Cmd+C
            // must arrive as the control byte, not as 'c' + a flag.
            if (ctrl || cmd), code >= 0x40, code <= 0x7E {
                code &= 0x1F
            }
            _ = St80InputController.shared.consumeActiveModifiers(0)
            st80_post_key_down(code, 0)
        }
    }

    func deleteBackward() {
        let mods = St80InputController.shared.consumeActiveModifiers(0)
        st80_post_key_down(8, mods)
    }
}

// Disable autocorrect / smart-quotes / predictive features — the
// Blue Book keyboard decoder wants raw ASCII, not "smart" text.
extension St80MTKView: UITextInputTraits {

    // Each accessor must return the UIKit value but also accept a
    // setter; UIKit calls them. Ignore the set.
    @objc var autocorrectionType: UITextAutocorrectionType {
        get { .no } set {}
    }
    @objc var autocapitalizationType: UITextAutocapitalizationType {
        get { .none } set {}
    }
    @objc var spellCheckingType: UITextSpellCheckingType {
        get { .no } set {}
    }
    @objc var smartQuotesType: UITextSmartQuotesType {
        get { .no } set {}
    }
    @objc var smartDashesType: UITextSmartDashesType {
        get { .no } set {}
    }
    @objc var smartInsertDeleteType: UITextSmartInsertDeleteType {
        get { .no } set {}
    }
    @objc var keyboardType: UIKeyboardType {
        get { .asciiCapable } set {}
    }
}
#endif

#if targetEnvironment(macCatalyst)
extension St80MTKView: UIPointerInteractionDelegate {
    // Hide the system pointer only when we have a Smalltalk cursor
    // bitmap to paint in its place. When we don't, return nil so
    // Catalyst shows the default macOS pointer.
    func pointerInteraction(_ interaction: UIPointerInteraction,
                            styleFor region: UIPointerRegion) -> UIPointerStyle? {
        return cursorOverlay?.image != nil ? .hidden() : nil
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
