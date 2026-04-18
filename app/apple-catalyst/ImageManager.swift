// st80-2026 — ImageManager.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Downloads, imports, renames, duplicates, and catalogs Smalltalk-80
// images inside Documents/Images/<slug>/. The catalog is persisted to
// Documents/image-library.json.
//
// Built-in templates pull individual files from
//
//     https://github.com/avwohl/st80-images/releases/download/<tag>/
//
// — a public companion repo that mirrors Wolczko's Xerox v2
// distribution. Each file is fetched directly via URLSession; no
// archive extraction is needed (unlike iospharo's Pharo zips).
//
// Custom-URL downloads accept any direct download link to a single
// image file; companion sources/changes have to be imported separately
// via "Add image from file…".
//
// Adapted from ../iospharo/iospharo/Image/ImageManager.swift.

import Foundation
import Combine

@MainActor
final class ImageManager: ObservableObject {

    static let shared = ImageManager()

    // MARK: - Templates

    struct Template: Identifiable {
        let id: String
        let label: String
        let slug: String
        let imageFileName: String
        let assetNames: [String]
        let baseURL: URL

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

    // MARK: - Private

    private let fm = FileManager.default
    private var downloadTask: Task<Void, Never>?
    private var activeURLTask: URLSessionDownloadTask?
    private var downloadSession: URLSession?
    private var downloadCancelled = false

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

        scanForUncatalogued()
        images.removeAll { !$0.exists }
        for i in images.indices {
            images[i].refreshSize()
        }
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

    /// Scan Images/ for subdirectories not yet in the catalog. Picks
    /// up images dropped in by hand or restored from a backup.
    private func scanForUncatalogued() {
        let root = St80Image.imagesRoot.path
        guard let dp = opendir(root) else { return }
        defer { closedir(dp) }

        while let entry = readdir(dp) {
            let dirName = withUnsafePointer(to: entry.pointee.d_name) { ptr in
                String(cString: UnsafeRawPointer(ptr)
                    .assumingMemoryBound(to: CChar.self))
            }
            guard !dirName.hasPrefix(".") else { continue }
            guard entry.pointee.d_type == DT_DIR else { continue }
            if images.contains(where: { $0.directoryName == dirName }) { continue }

            let subDir = St80Image.imagesRoot.appendingPathComponent(dirName)
            if let imageFile = Self.findFirstImageFile(in: subDir) {
                var img = St80Image.create(
                    name: dirName, directoryName: dirName,
                    imageFileName: imageFile, imageLabel: nil)
                img.refreshSize()
                images.append(img)
            }
        }
    }

    /// Find a likely Smalltalk-80 image file in a directory. Looks for
    /// VirtualImage / VirtualImageLSB / Smalltalk.image / *.image.
    private static func findFirstImageFile(in dir: URL) -> String? {
        let known = ["VirtualImage", "VirtualImageLSB",
                     "Smalltalk.image", "smalltalk.image"]
        for name in known {
            if FileManager.default
                .fileExists(atPath: dir.appendingPathComponent(name).path) {
                return name
            }
        }
        guard let dp = opendir(dir.path) else { return nil }
        defer { closedir(dp) }
        while let entry = readdir(dp) {
            let name = withUnsafePointer(to: entry.pointee.d_name) { ptr in
                String(cString: UnsafeRawPointer(ptr)
                    .assumingMemoryBound(to: CChar.self))
            }
            if name.hasSuffix(".image") && !name.hasPrefix(".") {
                return name
            }
        }
        return nil
    }

    // MARK: - Download (built-in template, multi-asset)

    func downloadTemplate(_ template: Template) {
        guard !isDownloading else { return }

        isDownloading = true
        downloadProgress = 0
        errorMessage = nil
        statusMessage = "Preparing download…"
        downloadCancelled = false

        let destDir = St80Image.imagesRoot
            .appendingPathComponent(template.slug, isDirectory: true)
        try? fm.createDirectory(at: destDir, withIntermediateDirectories: true)

        downloadTask = Task { [fm] in
            do {
                let total = template.assetNames.count
                for (i, name) in template.assetNames.enumerated() {
                    if Task.isCancelled || self.downloadCancelled { break }
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

                if Task.isCancelled || self.downloadCancelled {
                    await MainActor.run {
                        self.statusMessage = "Download cancelled"
                        self.isDownloading = false
                        self.downloadTask = nil
                    }
                    return
                }

                await MainActor.run {
                    var entry = St80Image.create(
                        name: template.label,
                        directoryName: template.slug,
                        imageFileName: template.imageFileName,
                        imageLabel: template.label)
                    entry.refreshSize()
                    self.images.removeAll { $0.directoryName == template.slug }
                    self.images.append(entry)
                    self.selectedImageID = entry.id
                    self.save()
                    self.statusMessage = nil
                    self.isDownloading = false
                    self.downloadTask = nil
                }
            } catch {
                await MainActor.run {
                    self.errorMessage =
                        "Download failed: \(error.localizedDescription)"
                    self.statusMessage = nil
                    self.isDownloading = false
                    self.downloadTask = nil
                }
            }
        }
    }

    // MARK: - Download (custom URL, single file)

    func downloadCustomURL(_ url: URL, label: String? = nil) {
        guard !isDownloading else { return }

        isDownloading = true
        downloadProgress = 0
        errorMessage = nil
        downloadCancelled = false
        let displayName = label ?? url.lastPathComponent
        statusMessage = "Downloading \(displayName)…"

        let session = URLSession(configuration: .default)
        downloadSession = session
        let task = session.downloadTask(with: url) { [weak self] tmp, _, err in
            Task { @MainActor in
                self?.handleCustomDownload(tmp: tmp, error: err,
                                           sourceURL: url, label: displayName)
            }
        }
        // Observe progress on the task.
        let obs = task.progress.observe(\.fractionCompleted) { [weak self] p, _ in
            Task { @MainActor in
                self?.downloadProgress = p.fractionCompleted
            }
        }
        // Hold onto the observation until the task completes.
        objc_setAssociatedObject(task, &Self.progressKey, obs,
                                 .OBJC_ASSOCIATION_RETAIN_NONATOMIC)
        activeURLTask = task
        task.resume()
    }

    private static var progressKey: UInt8 = 0

    private func handleCustomDownload(tmp: URL?, error: Error?,
                                      sourceURL: URL, label: String) {
        defer {
            isDownloading = false
            activeURLTask = nil
            downloadSession?.finishTasksAndInvalidate()
            downloadSession = nil
        }

        if let error = error {
            if (error as NSError).code == NSURLErrorCancelled {
                statusMessage = "Download cancelled"
            } else {
                errorMessage = "Download failed: \(error.localizedDescription)"
                statusMessage = nil
            }
            return
        }
        guard let tmp = tmp else {
            errorMessage = "Download produced no file"
            statusMessage = nil
            return
        }

        let fileName = sourceURL.lastPathComponent.isEmpty
            ? "VirtualImage" : sourceURL.lastPathComponent
        let baseName = (fileName as NSString).deletingPathExtension
        let slug = Self.makeSlug(from: baseName)
            + "-" + UUID().uuidString.prefix(4).lowercased()
        let destDir = St80Image.imagesRoot
            .appendingPathComponent(slug, isDirectory: true)

        do {
            try fm.createDirectory(at: destDir,
                                   withIntermediateDirectories: true)
            let dst = destDir.appendingPathComponent(fileName)
            try fm.moveItem(at: tmp, to: dst)

            var entry = St80Image.create(
                name: label, directoryName: slug,
                imageFileName: fileName, imageLabel: nil)
            entry.refreshSize()
            images.append(entry)
            selectedImageID = entry.id
            save()
            statusMessage = nil
        } catch {
            errorMessage = "Saving download failed: \(error.localizedDescription)"
            statusMessage = nil
        }
    }

    func cancelDownload() {
        downloadCancelled = true
        downloadTask?.cancel()
        downloadTask = nil
        activeURLTask?.cancel()
        activeURLTask = nil
        downloadSession?.invalidateAndCancel()
        downloadSession = nil
        if isDownloading {
            isDownloading = false
            statusMessage = "Download cancelled"
        }
    }

    // MARK: - Import

    func importImage(from url: URL) {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }

        let imageFile = url.lastPathComponent
        let base = (imageFile as NSString).deletingPathExtension
        let slug = Self.makeSlug(from: base.isEmpty ? imageFile : base)
            + "-" + UUID().uuidString.prefix(4).lowercased()
        let destDir = St80Image.imagesRoot
            .appendingPathComponent(slug, isDirectory: true)

        do {
            try fm.createDirectory(at: destDir,
                                   withIntermediateDirectories: true)
            try fm.copyItem(at: url,
                            to: destDir.appendingPathComponent(imageFile))

            let srcDir = url.deletingLastPathComponent()
            for companion in ["Smalltalk-80.sources", "Smalltalk-80.changes",
                              "\(base).sources", "\(base).changes"] {
                let src = srcDir.appendingPathComponent(companion)
                if fm.fileExists(atPath: src.path) {
                    try? fm.copyItem(at: src,
                        to: destDir.appendingPathComponent(companion))
                }
            }

            var entry = St80Image.create(
                name: base.isEmpty ? "Imported image" : base,
                directoryName: slug,
                imageFileName: imageFile,
                imageLabel: nil)
            entry.refreshSize()
            images.append(entry)
            selectedImageID = entry.id
            save()
        } catch {
            errorMessage = "Import failed: \(error.localizedDescription)"
        }
    }

    private static func makeSlug(from name: String) -> String {
        let allowed = Set("abcdefghijklmnopqrstuvwxyz0123456789-")
        let lower = name.lowercased()
        var out = ""
        for ch in lower {
            if allowed.contains(ch) { out.append(ch) }
            else if ch == " " || ch == "_" || ch == "." { out.append("-") }
        }
        return out.isEmpty ? "image" : String(out.prefix(40))
    }

    // MARK: - Delete / rename / duplicate / launch tracking

    func deleteImage(_ image: St80Image) {
        try? fm.removeItem(at: image.directoryURL)
        if UserDefaults.standard.string(forKey: "st80.autoLaunchImageID")
            == image.id.uuidString {
            UserDefaults.standard.removeObject(forKey: "st80.autoLaunchImageID")
        }
        images.removeAll { $0.id == image.id }
        if selectedImageID == image.id {
            selectedImageID = images.first?.id
        }
        save()
    }

    func renameImage(_ image: St80Image, to newName: String) {
        guard let idx = images.firstIndex(where: { $0.id == image.id }) else { return }
        let trimmed = newName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        images[idx].name = trimmed
        save()
    }

    func duplicateImage(_ image: St80Image) {
        let baseSlug = Self.makeSlug(from: image.name + "-copy")
        let slug = baseSlug + "-" + UUID().uuidString.prefix(4).lowercased()
        let destDir = St80Image.imagesRoot
            .appendingPathComponent(slug, isDirectory: true)

        do {
            try fm.copyItem(at: image.directoryURL, to: destDir)
            var entry = St80Image.create(
                name: "\(image.name) (copy)",
                directoryName: slug,
                imageFileName: image.imageFileName,
                imageLabel: image.imageLabel)
            entry.refreshSize()
            images.append(entry)
            selectedImageID = entry.id
            save()
        } catch {
            errorMessage = "Duplicate failed: \(error.localizedDescription)"
        }
    }

    func totalSizeForImage(_ image: St80Image) -> Int64? {
        guard let contents = try? fm.contentsOfDirectory(
            at: image.directoryURL,
            includingPropertiesForKeys: [.fileSizeKey]) else { return nil }
        var total: Int64 = 0
        for url in contents {
            if let size = try? url.resourceValues(forKeys: [.fileSizeKey])
                .fileSize {
                total += Int64(size)
            }
        }
        return total > 0 ? total : nil
    }

    func markLaunched(_ image: St80Image) {
        guard let idx = images.firstIndex(where: { $0.id == image.id }) else { return }
        images[idx].lastLaunchedAt = Date()
        selectedImageID = image.id
        save()
    }
}
