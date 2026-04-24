// st80-2026 — ControlStripView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Vertical control strip pinned to the trailing-or-leading edge of
// the VM view on iOS (see `stripOnRight` in ContentView — the strip
// moves opposite the camera notch / Dynamic Island on each orientation
// change). The iPad soft keyboard has no Ctrl — central in Smalltalk
// for editor commands — so we provide a virtual Ctrl toggle plus a
// show/hide keyboard toggle, raw-key buttons (Esc / Tab / Backspace)
// and one-tap shortcuts for the Blue Book text editor (Do it, Print
// it, Inspect, Accept, Cancel, Cut, Copy, Paste). Pattern ported
// from iospharo's ContentView (MIT, same author — see
// THIRD_PARTY_LICENSES). The action buttons emit ASCII control
// codes directly since the Xerox image's decoded keyboard has no
// modifier side-channel — see St80InputController.sendCtrlLetter.
//
// Geometry: iPad has a tiny R=18 squircle corner (< 2pt intrusion
// at x=4) so a static top inset works. iPhone in landscape needs
// per-device math — the n=5 superellipse formula from
// `../claude-skills/skills/device-geometry.md`. We split the strip
// into "top zone" (above Dynamic Island) and "bottom zone" (below
// DI, above home indicator) and centre each button group in its
// zone so buttons never land on top of the DI or get clipped by a
// corner.

import SwiftUI
import UIKit

#if !targetEnvironment(macCatalyst)

struct ControlStripView: View {

    @ObservedObject private var controller = St80InputController.shared

    fileprivate static let buttonSize: CGFloat = 40
    fileprivate static let actionSize: CGFloat = 26
    fileprivate static let buttonSpacing: CGFloat = 6
    fileprivate static let actionSpacing: CGFloat = 3
    fileprivate static let stripWidth: CGFloat = 48

    private var layout: StripLayout { StripLayout.current() }

    var body: some View {
        VStack(spacing: 0) {
            Color.clear.frame(height: layout.topInset)

            topGroup

            if layout.splitZones {
                Spacer()
                    .frame(minHeight: layout.zoneGap)
                // Action/raw-key stack is taller than the post-DI zone on
                // most DI iPhones in landscape. Let it scroll so every
                // button stays reachable even when the strip would
                // otherwise clip.
                ScrollView(.vertical, showsIndicators: false) {
                    bottomGroup
                }
            } else {
                separator
                ScrollView(.vertical, showsIndicators: false) {
                    bottomGroup
                }
            }
        }
        .padding(.bottom, layout.bottomInset)
        .padding(.horizontal, 2)
        .frame(width: Self.stripWidth)
        .background(Color(.systemGray6).opacity(0.95))
    }

    // MARK: - Button groups

    private var topGroup: some View {
        VStack(spacing: Self.buttonSpacing) {
            StripButton(icon: "keyboard",
                        isActive: controller.keyboardVisible,
                        size: Self.buttonSize,
                        tooltip: "Show/Hide keyboard") {
                controller.setKeyboardVisible(!controller.keyboardVisible)
            }
            StripButton(icon: "control",
                        isActive: controller.ctrlActive,
                        size: Self.buttonSize,
                        tooltip: "Toggle Ctrl") {
                controller.toggleCtrl()
            }
            StripButton(icon: "command",
                        isActive: controller.cmdActive,
                        size: Self.buttonSize,
                        tooltip: "Toggle Cmd") {
                controller.toggleCmd()
            }
        }
    }

    private var bottomGroup: some View {
        VStack(spacing: Self.actionSpacing) {
            StripButton(icon: "delete.left",
                        size: Self.actionSize,
                        tooltip: "Backspace") {
                controller.sendRaw(8)
            }
            StripButton(icon: "play.fill",
                        size: Self.actionSize,
                        tooltip: "Do it (Ctrl-D)") {
                controller.sendCtrlLetter("d")
            }
            StripButton(icon: "text.append",
                        size: Self.actionSize,
                        tooltip: "Print it (Ctrl-P)") {
                controller.sendCtrlLetter("p")
            }
            StripButton(icon: "eyeglasses",
                        size: Self.actionSize,
                        tooltip: "Inspect (Ctrl-Q)") {
                controller.sendCtrlLetter("q")
            }
            StripButton(icon: "checkmark.circle",
                        size: Self.actionSize,
                        tooltip: "Accept (Ctrl-S)") {
                controller.sendCtrlLetter("s")
            }
            StripButton(icon: "xmark.circle",
                        size: Self.actionSize,
                        tooltip: "Cancel (Ctrl-L)") {
                controller.sendCtrlLetter("l")
            }
            StripButton(icon: "scissors",
                        size: Self.actionSize,
                        tooltip: "Cut to clipboard") {
                controller.copySelectionToSystemClipboard(cut: true)
            }
            StripButton(icon: "doc.on.doc",
                        size: Self.actionSize,
                        tooltip: "Copy to clipboard") {
                controller.copySelectionToSystemClipboard(cut: false)
            }
            StripButton(icon: "doc.on.clipboard",
                        size: Self.actionSize,
                        tooltip: "Paste from clipboard") {
                controller.pasteFromSystemClipboard()
            }
        }
    }

    private var separator: some View {
        Rectangle()
            .fill(Color.gray.opacity(0.3))
            .frame(height: 1)
            .padding(.vertical, 6)
    }
}

struct StripButton: View {

    let label: String?
    let icon: String?
    let isActive: Bool
    let size: CGFloat
    let tooltip: String?
    let action: () -> Void

    init(label: String? = nil,
         icon: String? = nil,
         isActive: Bool = false,
         size: CGFloat = 40,
         tooltip: String? = nil,
         action: @escaping () -> Void) {
        self.label = label
        self.icon = icon
        self.isActive = isActive
        self.size = size
        self.tooltip = tooltip
        self.action = action
    }

    var body: some View {
        Button(action: action) {
            Group {
                if let icon = icon {
                    Image(systemName: icon)
                        .font(.system(size: size * 0.4))
                } else if let label = label {
                    Text(label)
                        .font(.system(size: size * 0.29,
                                      weight: .semibold,
                                      design: .rounded))
                }
            }
            .foregroundColor(isActive ? .white : .primary)
            .frame(width: size, height: size)
            .background(isActive ? Color.blue : Color.gray.opacity(0.2))
            .cornerRadius(size * 0.22)
        }
    }
}

// MARK: - Geometry

// Runtime strip layout derived from the active `UIWindow`'s
// `safeAreaInsets`. The squircle corner intrusion formula is from
// `claude-skills/skills/device-geometry.md` — (R - x)^5 + (R - y)^5
// = R^5 solved for y, with R inferred from the safe-area leading
// inset. DO NOT hand-pick padding constants — they'd go stale on
// every new device.
fileprivate struct StripLayout {

    let topInset: CGFloat
    let bottomInset: CGFloat
    let zoneGap: CGFloat
    let splitZones: Bool    // true on DI iPhones in landscape

    static func current() -> StripLayout {
        let idiom = UIDevice.current.userInterfaceIdiom

        guard let window = mainWindow() else {
            return StripLayout(topInset: 28, bottomInset: 8,
                               zoneGap: 0, splitZones: false)
        }

        if idiom == .pad {
            // iPad R=18: squircle intrusion < 2pt at the button's
            // left edge. A static 28pt top clears the home-button-less
            // safe area and the Mac-inspired menu bar the VM draws at
            // the top of its display.
            return StripLayout(
                topInset: max(28, window.safeAreaInsets.top + 8),
                bottomInset: max(window.safeAreaInsets.bottom, 6),
                zoneGap: 0,
                splitZones: false)
        }

        // iPhone: compute from safeAreaInsets.
        let saLeading = max(window.safeAreaInsets.left,
                            window.safeAreaInsets.right)
        let saBottom = window.safeAreaInsets.bottom
        let R = estimatedCornerRadius(saLeading: saLeading)
        let hasDI = saLeading > 55

        // Button left edge in a `stripWidth`-wide strip.
        let buttonLeftX = (ControlStripView.stripWidth
                           - ControlStripView.buttonSize) / 2
        let intrusion = squircleIntrusion(R: R, x: buttonLeftX)

        let topInset = ceil(intrusion + 2)
        let bottomCornerClear = ceil(intrusion + 2)
        let bottomInset = bottomCornerClear + saBottom

        if !hasDI {
            return StripLayout(topInset: topInset,
                               bottomInset: bottomInset,
                               zoneGap: 0,
                               splitZones: false)
        }

        // Split zones around the Dynamic Island. In landscape the
        // screen's narrow dimension is the "height"; the DI is
        // vertically centred there with ~127pt extent.
        let landscapeHeight = UIScreen.main.nativeBounds.width
            / UIScreen.main.nativeScale
        let diExtent: CGFloat = 126.9
        let diTopY = landscapeHeight / 2 - diExtent / 2
        let topGroupHeight = 3 * ControlStripView.buttonSize
            + 2 * ControlStripView.buttonSpacing

        // Gap between bottom of top group and top of bottom group.
        let zoneGap = max(0, diTopY - topInset - topGroupHeight)

        return StripLayout(topInset: topInset,
                           bottomInset: bottomInset,
                           zoneGap: zoneGap,
                           splitZones: true)
    }

    /// SA leading → R map, per the skill doc's device table.
    private static func estimatedCornerRadius(saLeading: CGFloat) -> CGFloat {
        if saLeading >= 60 { return 62 }
        if saLeading >= 55 { return 55 }
        if saLeading >= 48 { return 47.33 }
        if saLeading >= 47 { return 47.33 }
        if saLeading >= 44 { return 39 }
        return 0
    }

    /// Exact y-clearance for a squircle corner: `y = R - (R^5 - (R-x)^5)^(1/5)`.
    /// Skill doc calls for n=5. Ceil() applied at the call site.
    private static func squircleIntrusion(R: CGFloat, x: CGFloat) -> CGFloat {
        guard R > 0 else { return 0 }
        guard x > 0 else { return R }
        if x >= R { return 0 }
        let r5 = pow(R, 5)
        let d5 = pow(R - x, 5)
        return R - pow(r5 - d5, 0.2)
    }

    private static func mainWindow() -> UIWindow? {
        UIApplication.shared.connectedScenes
            .compactMap { $0 as? UIWindowScene }.first?
            .windows.first
    }
}

#endif
