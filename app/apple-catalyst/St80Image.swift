// st80-2026 — St80Image.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Minimal model for a Smalltalk-80 image on disk. Sandbox-friendly:
// every image lives in its own subdirectory under
// Documents/Images/<slug>/, and the PosixFileSystem VM-side uses
// that subdirectory as its root so the image's `sources` / `changes`
// file primitives resolve to the same place.

import Foundation

struct St80Image: Codable, Identifiable {
    let id: UUID
    var name: String
    var directoryName: String     // relative to Documents/Images/
    var imageFileName: String     // e.g. "VirtualImage"
    var addedAt: Date

    static var imagesRoot: URL {
        let docs = FileManager.default.urls(
            for: .documentDirectory, in: .userDomainMask).first!
        return docs.appendingPathComponent("Images", isDirectory: true)
    }

    var directoryURL: URL {
        Self.imagesRoot.appendingPathComponent(directoryName, isDirectory: true)
    }

    var imageURL: URL {
        directoryURL.appendingPathComponent(imageFileName)
    }

    var imagePath: String { imageURL.path }

    var exists: Bool {
        FileManager.default.fileExists(atPath: imagePath)
    }
}
