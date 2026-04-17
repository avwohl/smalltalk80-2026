// st80-2026 — ControlStripView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Vertical control strip pinned to the leading edge of the VM view
// on iOS. The iPad soft keyboard has no Ctrl or Cmd — both are
// central in Smalltalk (Ctrl for editor commands, Cmd for system
// actions) — so we provide virtual toggles here, plus Esc / Tab /
// Backspace and a show/hide toggle for the keyboard itself.
//
// StripButton + layout pattern ported verbatim from iospharo's
// ContentView.swift (MIT, same author — see THIRD_PARTY_LICENSES).
// Trimmed for Blue Book: no DoIt/PrintIt/InspectIt/Debug buttons
// (those are Pharo shortcuts, not 1983 Smalltalk-80 ones).
//
// Geometry (corner squircle, Dynamic Island, home indicator) lives
// in a follow-up phase; current layout uses a static 28pt top inset
// that clears the iPad safe area + a little breathing room.

import SwiftUI

#if !targetEnvironment(macCatalyst)

struct ControlStripView: View {

    @ObservedObject private var controller = St80InputController.shared

    private let buttonSize: CGFloat = 40
    private let buttonSpacing: CGFloat = 6
    private let stripWidth: CGFloat = 48

    var body: some View {
        VStack(spacing: 0) {
            Color.clear.frame(height: 28)        // safe area + breathing
            VStack(spacing: buttonSpacing) {
                StripButton(icon: "keyboard",
                            isActive: controller.keyboardVisible,
                            size: buttonSize,
                            tooltip: "Show/Hide keyboard") {
                    controller.setKeyboardVisible(!controller.keyboardVisible)
                }
                StripButton(icon: "control",
                            isActive: controller.ctrlActive,
                            size: buttonSize,
                            tooltip: "Toggle Ctrl") {
                    controller.toggleCtrl()
                }
                StripButton(icon: "command",
                            isActive: controller.cmdActive,
                            size: buttonSize,
                            tooltip: "Toggle Cmd") {
                    controller.toggleCmd()
                }

                Rectangle()
                    .fill(Color.gray.opacity(0.3))
                    .frame(height: 1)
                    .padding(.vertical, 2)

                StripButton(label: "Esc", size: buttonSize, tooltip: "Escape") {
                    controller.sendRaw(27)
                }
                StripButton(label: "Tab", size: buttonSize, tooltip: "Tab") {
                    controller.sendRaw(9)
                }
                StripButton(icon: "delete.left",
                            size: buttonSize,
                            tooltip: "Backspace") {
                    controller.sendRaw(8)
                }

                Spacer()
            }
            .padding(.vertical, 4)
            .padding(.horizontal, 2)
            .background(Color(.systemGray6).opacity(0.95))
        }
        .frame(width: stripWidth)
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

#endif
