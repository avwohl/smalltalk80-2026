// st80-2026 — ContentView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Top-level screen switcher: show the image library until the user
// launches an image, then hand over to the Metal-backed VM view.

import SwiftUI

struct ContentView: View {

    @StateObject private var manager = ImageManager.shared
    @State private var launchedImagePath: String? = nil

    var body: some View {
        Group {
            if let path = launchedImagePath {
                MetalView(imagePath: path)
                    .ignoresSafeArea()
            } else {
                ImageLibraryView(manager: manager) { image in
                    launchedImagePath = image.imagePath
                    manager.selectedImageID = image.id
                }
            }
        }
    }
}
