// st80-2026 — AppDelegate.swift (Mac Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// UIApplicationMain entry. A single-scene UIKit app that hosts the
// SwiftUI `ContentView` via `UIHostingController`. The native macOS
// frontend in `../apple/` uses AppKit directly; this variant is for
// Catalyst and iOS.

import UIKit
import SwiftUI
import Foundation

func st80Log(_ s: String) {
    fputs("[st80] " + s + "\n", stderr)
    fflush(stderr)
}

@main
class AppDelegate: UIResponder, UIApplicationDelegate {

    func application(_ application: UIApplication,
                     didFinishLaunchingWithOptions launchOptions:
                     [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        st80Log("didFinishLaunching")
        return true
    }

    func application(_ application: UIApplication,
                     configurationForConnecting connectingSceneSession: UISceneSession,
                     options: UIScene.ConnectionOptions) -> UISceneConfiguration {
        let config = UISceneConfiguration(
            name: nil, sessionRole: connectingSceneSession.role)
        config.delegateClass = SceneDelegate.self
        return config
    }

    func applicationWillTerminate(_ application: UIApplication) {
        st80Log("applicationWillTerminate")
        st80_stop()
        st80_shutdown()
    }

    // Replace the stock "About Smalltalk80" menu item with one that
    // presents our SwiftUI AboutView and add a File → Export Current
    // Image menu command for Save-As-style image export.
    override func buildMenu(with builder: UIMenuBuilder) {
        super.buildMenu(with: builder)
#if targetEnvironment(macCatalyst)
        let about = UICommand(
            title: "About Smalltalk80",
            action: #selector(showAboutFromMenu))
        builder.replaceChildren(ofMenu: .about) { _ in [about] }

        let export = UIKeyCommand(
            title: "Export Current Image…",
            action: #selector(exportImageFromMenu),
            input: "e",
            modifierFlags: [.command, .shift])
        let exportMenu = UIMenu(title: "", options: .displayInline,
                                children: [export])
        builder.insertChild(exportMenu, atStartOfMenu: .file)
#endif
    }

    @objc func showAboutFromMenu() {
        NotificationCenter.default.post(name: .st80ShowAbout, object: nil)
    }

    @objc func exportImageFromMenu() {
        NotificationCenter.default.post(name: .st80ExportImage, object: nil)
    }
}

extension Notification.Name {
    static let st80ShowAbout    = Notification.Name("st80ShowAbout")
    static let st80ExportImage  = Notification.Name("st80ExportImage")
}

class SceneDelegate: UIResponder, UIWindowSceneDelegate {
    var window: UIWindow?

    func scene(_ scene: UIScene,
               willConnectTo session: UISceneSession,
               options connectionOptions: UIScene.ConnectionOptions) {
        guard let windowScene = scene as? UIWindowScene else { return }
        st80Log("scene willConnectTo")

#if targetEnvironment(macCatalyst)
        if let restrictions = windowScene.sizeRestrictions {
            restrictions.minimumSize = CGSize(width: 640, height: 480)
        }
        if let titlebar = windowScene.titlebar {
            titlebar.titleVisibility = .visible
        }
        scene.title = "Smalltalk80"
#endif

        let contentView = ContentView()
        let host = UIHostingController(rootView: contentView)

        window = UIWindow(windowScene: windowScene)
        window?.rootViewController = host
        window?.makeKeyAndVisible()
    }
}
