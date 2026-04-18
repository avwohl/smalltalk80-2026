// st80-2026 — St80Image.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Model for a Smalltalk-80 image on disk. Sandbox-friendly: every image
// lives in its own subdirectory under Documents/Images/<slug>/, and the
// PosixFileSystem VM-side uses that subdirectory as its root so the
// image's `sources` / `changes` file primitives resolve to the same
// place.
//
// Adapted from ../iospharo/iospharo/Image/PharoImage.swift, dropping
// the Pharo-version column (st80 has effectively one canonical image
// — the 1983 Xerox v2 distribution).

import Foundation

struct St80Image: Codable, Identifiable, Equatable {

    var id: UUID
    var name: String
    /// Subdirectory name under Images/ (slug)
    var directoryName: String
    /// Actual image filename inside the directory (e.g. "VirtualImage")
    var imageFileName: String
    /// Optional source label, e.g. "Xerox v2 (1983)", used in the
    /// "Source" column of the library table.
    var imageLabel: String?
    var addedAt: Date
    var lastLaunchedAt: Date?
    var imageSizeBytes: Int64?

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

    static func create(
        name: String,
        directoryName: String,
        imageFileName: String,
        imageLabel: String? = nil
    ) -> St80Image {
        St80Image(
            id: UUID(),
            name: name,
            directoryName: directoryName,
            imageFileName: imageFileName,
            imageLabel: imageLabel,
            addedAt: Date(),
            lastLaunchedAt: nil,
            imageSizeBytes: nil)
    }

    mutating func refreshSize() {
        let attrs = try? FileManager.default.attributesOfItem(atPath: imagePath)
        imageSizeBytes = attrs?[.size] as? Int64
    }

    var sourceLabel: String { imageLabel ?? "—" }

    var fileModificationDate: Date? {
        let attrs = try? FileManager.default.attributesOfItem(atPath: imagePath)
        return attrs?[.modificationDate] as? Date
    }

    var formattedSize: String? {
        guard let bytes = imageSizeBytes else { return nil }
        let formatter = ByteCountFormatter()
        formatter.countStyle = .file
        return formatter.string(fromByteCount: bytes)
    }
}
