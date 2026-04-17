// st80-2026 — main.swift
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Classic NSApplicationMain-style entry point. Used instead of
// SwiftUI `@main` because `swiftc -parse-as-library` + `@main`
// alone (without Xcode's build-phase shim) does not reliably
// instantiate the `WindowGroup`'s Scene body; the process starts
// but no content window is created. Hosting SwiftUI in an explicit
// `NSHostingView` inside an `NSWindow` is rock-solid.

import AppKit
import SwiftUI
import Foundation

func st80Log(_ s: String) {
    fputs("[st80] " + s + "\n", stderr)
    fflush(stderr)
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    var window: NSWindow!

    func applicationDidFinishLaunching(_ note: Notification) {
        st80Log("applicationDidFinishLaunching")

        // Fail early with a friendly popup if the image isn't on
        // disk. Without this, st80_init silently fails deep inside
        // MetalRenderer.setup and the user sees a blank window.
        if let missing = Self.firstMissingImage() {
            presentImageMissingAlert(path: missing)
            NSApp.terminate(nil)
            return
        }

        let contentView = ContentView()
            .frame(minWidth: 640, minHeight: 480)

        window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 1024, height: 768),
            styleMask: [.titled, .closable, .resizable, .miniaturizable],
            backing: .buffered,
            defer: false
        )
        let imageName = (CommandLine.arguments.dropFirst().first { !$0.hasPrefix("-") })
                       .map { ($0 as NSString).lastPathComponent } ?? ""
        window.title = imageName.isEmpty ? "Smalltalk-80" : "Smalltalk-80 — \(imageName)"
        window.contentView = NSHostingView(rootView: contentView)
        window.center()
        window.setFrameAutosaveName("St80Window")
        window.makeKeyAndOrderFront(nil)

        // --demo-click: fire an in-process yellow-click at the centre
        // of the VM display a few seconds after startup. Events
        // originate inside our own process, so Accessibility
        // permissions don't apply — useful for automated visual
        // verification via screencapture.
        if CommandLine.arguments.contains("--demo-click") {
            DispatchQueue.main.asyncAfter(deadline: .now() + 4) {
                let w = st80_display_width()
                let h = st80_display_height()
                guard w > 0, h > 0 else {
                    st80Log("--demo-click: display not ready")
                    return
                }
                let cx = Int32(w / 2)
                let cy = Int32(h / 2)
                st80Log("--demo-click: yellow-press at (\(cx),\(cy))")
                st80_post_mouse_move(cx, cy)
                st80_post_mouse_down(cx, cy, ST80_BTN_YELLOW)
                // Hold 8 s so external screencap tools have a window
                // of time to grab the opened menu. Automated visual
                // tests key off this timing.
                DispatchQueue.main.asyncAfter(deadline: .now() + 8.0) {
                    st80_post_mouse_up(cx, cy, ST80_BTN_YELLOW)
                    st80Log("--demo-click: released")
                }
            }
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        true
    }

    func applicationWillTerminate(_ note: Notification) {
        st80Log("applicationWillTerminate")
        st80_stop()
        st80_shutdown()
    }

    /// Returns the first image path passed to the app (or the
    /// fallback `reference/xerox-image/VirtualImage`) that doesn't
    /// exist on disk. nil means the image is present.
    private static func firstMissingImage() -> String? {
        let cliArg = CommandLine.arguments.dropFirst().first { !$0.hasPrefix("-") }
        let envVar = ProcessInfo.processInfo.environment["ST80_IMAGE"]
        let path = cliArg ?? envVar ?? "reference/xerox-image/VirtualImage"
        return FileManager.default.fileExists(atPath: path) ? nil : path
    }

    private func presentImageMissingAlert(path: String) {
        let alert = NSAlert()
        alert.messageText = "Smalltalk-80 image not found"
        alert.informativeText = """
            Cannot open \(path).

            To fetch the Xerox 1983 virtual image:

              mkdir -p reference/xerox-image
              curl -sSLo reference/xerox-image/image.tar.gz \\
                   http://www.wolczko.com/st80/image.tar.gz
              (cd reference/xerox-image && tar xzf image.tar.gz)

            Or pass a path to the image as a CLI argument:
              open St80.app --args /full/path/to/VirtualImage
            """
        alert.alertStyle = .warning
        alert.addButton(withTitle: "Quit")
        alert.runModal()
    }
}

st80Log("main: args=\(CommandLine.arguments)")

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.regular)
app.activate(ignoringOtherApps: true)
app.run()
