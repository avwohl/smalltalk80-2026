// st80-2026 — ImageManager.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Downloads and catalogs Smalltalk-80 image files inside
// Documents/Images/. The asset host is
//
//     https://github.com/avwohl/st80-images/releases/download/<tag>/
//
// — a public companion repo that mirrors Wolczko's Xerox v2
// distribution. Individual files (VirtualImage, Smalltalk-80.sources,
// trace2, trace3) are served as release assets, so we can GET each
// one directly via URLSession with no archive extraction.
//
// Modelled on ../iospharo/iospharo/Image/ImageManager.swift but
// trimmed to what Phase 2 actually needs: one built-in template,
// download-to-Documents, simple catalog persisted as JSON.

import Foundation
import Combine

@MainActor
final class ImageManager: ObservableObject {

    static let shared = ImageManager()

    // MARK: - Templates

    struct Template: Identifiable {
        let id: String
        let label: String
        let slug: String            // directory name under Images/
        let imageFileName: String
        let assetNames: [String]    // file names to download from release
        let baseURL: URL            // release-assets base URL

        static let builtIn: [Template] = [
            Template(
                id: "xerox-v2",
                label: "Xerox Smalltalk-80 v2 (1983)",
                slug: "xerox-v2",
                imageFileName: "VirtualImage",
                assetNames: ["VirtualImage", "Smalltalk-80.sources"],
                baseURL: URL(string:
                    "https://github.com/avwohl/st80-images/releases/download/xerox-v2/")!
            )
        ]
    }

    // MARK: - Published state

    @Published var images: [St80Image] = []
    @Published var selectedImageID: UUID?
    @Published var isDownloading = false
    @Published var downloadProgress: Double = 0       // 0.0 – 1.0
    @Published var statusMessage: String?
    @Published var errorMessage: String?

    // MARK: - Paths

    private let fm = FileManager.default

    private var documentsDirectory: URL {
        fm.urls(for: .documentDirectory, in: .userDomainMask).first!
    }
    private var catalogURL: URL {
        documentsDirectory.appendingPathComponent("image-library.json")
    }

    var selectedImage: St80Image? {
        if let id = selectedImageID,
           let match = images.first(where: { $0.id == id }) {
            return match
        }
        return images.first
    }

    // MARK: - Load / save catalog

    func load() {
        try? fm.createDirectory(at: St80Image.imagesRoot,
                                withIntermediateDirectories: true)

        let decoder = JSONDecoder()
        decoder.dateDecodingStrategy = .iso8601
        if let data = try? Data(contentsOf: catalogURL),
           let saved = try? decoder.decode([St80Image].self, from: data) {
            images = saved
        }

        // Drop entries whose files disappeared (e.g. user cleared Documents).
        images.removeAll { !$0.exists }
        save()
    }

    func save() {
        let encoder = JSONEncoder()
        encoder.outputFormatting = .prettyPrinted
        encoder.dateEncodingStrategy = .iso8601
        if let data = try? encoder.encode(images) {
            try? data.write(to: catalogURL, options: .atomic)
        }
    }

    // MARK: - Download

    /// Download every asset of the given template to
    /// `Documents/Images/<slug>/`, then register the image in the
    /// catalog and select it. Safe to call concurrently — the second
    /// call is dropped while the first is in progress.
    func downloadTemplate(_ template: Template) {
        guard !isDownloading else { return }

        isDownloading = true
        downloadProgress = 0
        errorMessage = nil
        statusMessage = "Preparing download…"

        let destDir = St80Image.imagesRoot
            .appendingPathComponent(template.slug, isDirectory: true)
        try? fm.createDirectory(at: destDir, withIntermediateDirectories: true)

        Task { [fm] in
            do {
                let total = template.assetNames.count
                for (i, name) in template.assetNames.enumerated() {
                    await MainActor.run {
                        self.statusMessage =
                            "Downloading \(name) (\(i + 1) of \(total))…"
                    }
                    let src = template.baseURL.appendingPathComponent(name)
                    let (tmp, _) = try await URLSession.shared.download(from: src)
                    let dst = destDir.appendingPathComponent(name)
                    try? fm.removeItem(at: dst)
                    try fm.moveItem(at: tmp, to: dst)
                    await MainActor.run {
                        self.downloadProgress = Double(i + 1) / Double(total)
                    }
                }

                await MainActor.run {
                    let entry = St80Image(
                        id: UUID(),
                        name: template.label,
                        directoryName: template.slug,
                        imageFileName: template.imageFileName,
                        addedAt: Date())
                    // Replace any older copy with the same slug.
                    self.images.removeAll { $0.directoryName == template.slug }
                    self.images.append(entry)
                    self.selectedImageID = entry.id
                    self.save()
                    self.statusMessage = nil
                    self.isDownloading = false
                }
            } catch {
                await MainActor.run {
                    self.errorMessage = "Download failed: \(error.localizedDescription)"
                    self.statusMessage = nil
                    self.isDownloading = false
                }
            }
        }
    }

    // MARK: - Delete

    func deleteImage(_ image: St80Image) {
        try? fm.removeItem(at: image.directoryURL)
        images.removeAll { $0.id == image.id }
        if selectedImageID == image.id {
            selectedImageID = images.first?.id
        }
        save()
    }
}
