// st80-2026 — ImageLibraryView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Pharo-Launcher-style picker: sortable column headers, filter box,
// per-row context menu, detail panel under the table, error banner
// overlay, and a project-info bar at the top. Adapted from
// ../iospharo/iospharo/Views/ImageLibraryView.swift, with the
// Pharo-version column repurposed as a "Source" column and the
// Export-as-App pipeline omitted.

import SwiftUI
import UIKit
import UniformTypeIdentifiers

enum ImageSortOrder: String, CaseIterable {
    case name = "Name"
    case source = "Source"
    case size = "Size"
    case lastModified = "Last Modified"
}

struct ImageLibraryView: View {

    @ObservedObject var manager: ImageManager
    let onLaunch: (St80Image) -> Void

    @State private var showingAbout = false
    @State private var showingSettings = false
    @State private var showingNewImage = false
    @State private var showingImporter = false
    @State private var imageToDelete: St80Image?
    @State private var imageToRename: St80Image?
    @State private var renameText: String = ""
    @State private var imageToShare: St80Image?
    @State private var sortOrder: ImageSortOrder = .name
    @State private var sortAscending: Bool = true
    @State private var filterText: String = ""
    @State private var selectedRowID: UUID?

    @AppStorage("st80.autoLaunchImageID") private var autoLaunchImageID: String?

    private var filteredImages: [St80Image] {
        let base = filterText.isEmpty
            ? manager.images
            : manager.images.filter {
                $0.name.localizedCaseInsensitiveContains(filterText)
              }

        return base.sorted { a, b in
            let asc: Bool
            switch sortOrder {
            case .name:
                asc = a.name.localizedCaseInsensitiveCompare(b.name)
                    == .orderedAscending
            case .source:
                asc = (a.imageLabel ?? "")
                    .localizedCaseInsensitiveCompare(b.imageLabel ?? "")
                    == .orderedAscending
            case .size:
                asc = (a.imageSizeBytes ?? 0) < (b.imageSizeBytes ?? 0)
            case .lastModified:
                asc = (a.fileModificationDate ?? .distantPast)
                    < (b.fileModificationDate ?? .distantPast)
            }
            return sortAscending ? asc : !asc
        }
    }

    private var selectedRow: St80Image? {
        guard let id = selectedRowID else { return nil }
        return manager.images.first { $0.id == id }
    }

    var body: some View {
        NavigationView {
            VStack(spacing: 0) {
                if manager.images.isEmpty && !manager.isDownloading {
                    emptyState
                } else {
                    projectInfoBar
                    filterBar
                    Divider()
                    columnHeaders
                    Divider()

                    if manager.isDownloading {
                        DownloadProgressRow(manager: manager)
                            .padding(.horizontal, 12)
                            .padding(.vertical, 6)
                        Divider()
                    }

                    imageTable
                    Divider()
                    detailPanel
                }
            }
            .navigationTitle("Smalltalk-80")
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    Button {
                        showingSettings = true
                    } label: {
                        Image(systemName: "gear")
                    }
                    .accessibilityLabel("Settings")
                }
                ToolbarItem(placement: .primaryAction) {
                    Menu {
                        Button {
                            showingNewImage = true
                        } label: {
                            Label("Download…", systemImage: "arrow.down.circle")
                        }
                        Button {
                            showingImporter = true
                        } label: {
                            Label("Add image from file…", systemImage: "folder")
                        }
                    } label: {
                        Image(systemName: "plus")
                    }
                    .accessibilityLabel("Add image")
                }
            }
            .sheet(isPresented: $showingAbout) { AboutView() }
            .sheet(isPresented: $showingSettings) { SettingsView() }
            .sheet(isPresented: $showingNewImage) {
                NewImageView(manager: manager)
            }
            .fileImporter(isPresented: $showingImporter,
                          allowedContentTypes: [.data],
                          allowsMultipleSelection: false) { result in
                if case .success(let urls) = result, let url = urls.first {
                    manager.importImage(from: url)
                } else if case .failure(let err) = result {
                    manager.errorMessage =
                        "Import failed: \(err.localizedDescription)"
                }
            }
            .alert("Delete Image?", isPresented: .init(
                get: { imageToDelete != nil },
                set: { if !$0 { imageToDelete = nil } }
            )) {
                Button("Delete", role: .destructive) {
                    if let image = imageToDelete {
                        manager.deleteImage(image)
                    }
                    imageToDelete = nil
                }
                Button("Cancel", role: .cancel) { imageToDelete = nil }
            } message: {
                if let image = imageToDelete {
                    Text("This will permanently remove “\(image.name)” "
                        + "and every file in its directory.")
                }
            }
            .alert("Rename Image", isPresented: .init(
                get: { imageToRename != nil },
                set: { if !$0 { imageToRename = nil } }
            )) {
                TextField("Name", text: $renameText)
                Button("Rename") {
                    if let image = imageToRename {
                        manager.renameImage(image, to: renameText)
                    }
                    imageToRename = nil
                }
                Button("Cancel", role: .cancel) { imageToRename = nil }
            } message: {
                Text("Enter a new display name for this image.")
            }
            .sheet(item: $imageToShare) { image in
                ShareSheet(activityItems: [image.directoryURL])
            }
        }
        .navigationViewStyle(.stack)
        .overlay(alignment: .bottom) { errorBanner }
        .onAppear { manager.load() }
    }

    // MARK: - Project info bar

    private var projectInfoBar: some View {
        HStack(spacing: 4) {
            Text("Smalltalk-80 — Blue Book VM, 1983 Xerox image")
                .foregroundColor(.secondary)
            Spacer()
            Link("GitHub", destination: URL(string:
                "https://github.com/avwohl/smalltalk80-2026")!)
            Text("·").foregroundColor(.secondary)
            Text("v\(Bundle.main.infoDictionary?["CFBundleShortVersionString"] as? String ?? "?") "
                + "(\(Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "?"))")
                .foregroundColor(.secondary)
            Link("Changes", destination: URL(string:
                "https://github.com/avwohl/smalltalk80-2026/blob/main/docs/changes.md")!)
            Text("·").foregroundColor(.secondary)
            Link("Report a Bug", destination: URL(string:
                "https://github.com/avwohl/smalltalk80-2026/issues")!)
        }
        .font(.caption)
        .padding(.horizontal, 12)
        .padding(.vertical, 4)
        .background(Color(.secondarySystemBackground))
    }

    // MARK: - Filter bar

    private var filterBar: some View {
        HStack {
            Image(systemName: "magnifyingglass")
                .foregroundColor(.secondary)
            TextField("Filter images…", text: $filterText)
                .textFieldStyle(.plain)
            if !filterText.isEmpty {
                Button {
                    filterText = ""
                } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundColor(.secondary)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(Color(.systemBackground))
    }

    // MARK: - Column headers

    private var columnHeaders: some View {
        HStack(spacing: 0) {
            columnHeaderButton("Name", order: .name)
                .frame(maxWidth: .infinity, alignment: .leading)

            columnHeaderButton("Source", order: .source)
                .frame(width: ImageTableLayout.sourceWidth, alignment: .leading)

            columnHeaderButton("Size", order: .size)
                .frame(width: ImageTableLayout.sizeWidth, alignment: .trailing)

            columnHeaderButton("Last Modified", order: .lastModified)
                .frame(width: ImageTableLayout.lastModifiedWidth,
                       alignment: .leading)
                .padding(.leading, 12)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(Color(.secondarySystemBackground))
    }

    private func columnHeaderButton(_ title: String,
                                    order: ImageSortOrder) -> some View {
        Button {
            if sortOrder == order {
                sortAscending.toggle()
            } else {
                sortOrder = order
                sortAscending = true
            }
        } label: {
            HStack(spacing: 2) {
                Text(title)
                    .font(.caption)
                    .fontWeight(.semibold)
                    .foregroundColor(.primary)
                if sortOrder == order {
                    Image(systemName: sortAscending
                          ? "chevron.up" : "chevron.down")
                        .font(.system(size: 8, weight: .bold))
                        .foregroundColor(.accentColor)
                }
            }
        }
        .buttonStyle(.plain)
    }

    // MARK: - Empty state

    private var emptyState: some View {
        VStack(spacing: 24) {
            Spacer()

            Image(systemName: "square.stack.3d.up")
                .font(.system(size: 64))
                .foregroundColor(.secondary)

            Text("No image downloaded yet")
                .font(.title2)

            Text("Download the 1983 Xerox distribution or import "
                 + "an image you already have.")
                .font(.body)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal)

            VStack(spacing: 12) {
                ForEach(ImageManager.Template.builtIn) { template in
                    Button {
                        manager.downloadTemplate(template)
                    } label: {
                        Label("Download \(template.label)",
                              systemImage: "arrow.down.circle.fill")
                            .font(.headline)
                            .frame(maxWidth: 320)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .disabled(manager.isDownloading)
                }

                Button {
                    showingNewImage = true
                } label: {
                    Label("Download from URL…",
                          systemImage: "link")
                        .frame(maxWidth: 320)
                }
                .buttonStyle(.bordered)
                .controlSize(.large)
                .disabled(manager.isDownloading)

                Button {
                    showingImporter = true
                } label: {
                    Label("Add image from file…", systemImage: "folder")
                        .frame(maxWidth: 320)
                }
                .buttonStyle(.bordered)
                .controlSize(.large)
            }

            if manager.isDownloading {
                DownloadProgressRow(manager: manager)
                    .padding(.horizontal, 32)
            }

            Spacer()
        }
        .padding()
    }

    // MARK: - Image table

    private var imageTable: some View {
        ScrollView {
            LazyVStack(spacing: 0) {
                ForEach(filteredImages) { image in
                    ImageRow(
                        image: image,
                        isSelected: image.id == selectedRowID,
                        isAutoLaunch: autoLaunchImageID == image.id.uuidString)
                    .onTapGesture {
                        selectedRowID = image.id
                    }
                    .contextMenu {
                        rowContextMenu(image)
                    }
                    .swipeActions {
                        Button(role: .destructive) {
                            imageToDelete = image
                        } label: {
                            Label("Delete", systemImage: "trash")
                        }
                    }

                    Divider().padding(.leading, 12)
                }
            }
        }
        .frame(maxHeight: .infinity)
    }

    @ViewBuilder
    private func rowContextMenu(_ image: St80Image) -> some View {
        Button {
            launch(image)
        } label: {
            Label("Launch", systemImage: "play.fill")
        }

        Divider()

        if autoLaunchImageID == image.id.uuidString {
            Button {
                autoLaunchImageID = nil
            } label: {
                Label("Clear Auto-Launch", systemImage: "star.slash")
            }
        } else {
            Button {
                autoLaunchImageID = image.id.uuidString
            } label: {
                Label("Set as Auto-Launch", systemImage: "star.fill")
            }
        }

        Button {
            renameText = image.name
            imageToRename = image
        } label: {
            Label("Rename…", systemImage: "pencil")
        }

        Button {
            manager.duplicateImage(image)
        } label: {
            Label("Duplicate", systemImage: "doc.on.doc")
        }

        Button {
            imageToShare = image
        } label: {
            Label("Share…", systemImage: "square.and.arrow.up")
        }

        Button {
            showInFiles(image)
        } label: {
            Label("Show in Files", systemImage: "folder")
        }

        Divider()

        Button(role: .destructive) {
            imageToDelete = image
        } label: {
            Label("Delete…", systemImage: "trash")
        }
    }

    // MARK: - Detail panel

    private var detailPanel: some View {
        VStack(alignment: .leading, spacing: 8) {
            if let image = selectedRow {
                Text("\(image.name) — \(image.imageFileName)")
                    .font(.caption)
                    .foregroundColor(.white)
                    .lineLimit(1)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 6)
                    .background(Color.gray)

                HStack(spacing: 12) {
                    Button {
                        launch(image)
                    } label: {
                        Label("Launch", systemImage: "play.fill")
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.small)

                    Button {
                        renameText = image.name
                        imageToRename = image
                    } label: {
                        Label("Rename", systemImage: "pencil")
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)

                    Button {
                        imageToShare = image
                    } label: {
                        Label("Share", systemImage: "square.and.arrow.up")
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)

                    Button {
                        showInFiles(image)
                    } label: {
                        Label("Show in Files", systemImage: "folder")
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)

                    if autoLaunchImageID == image.id.uuidString {
                        Button {
                            autoLaunchImageID = nil
                        } label: {
                            Label("Auto-Launch", systemImage: "star.fill")
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                        .tint(.orange)
                    } else {
                        Button {
                            autoLaunchImageID = image.id.uuidString
                        } label: {
                            Label("Auto-Launch", systemImage: "star")
                        }
                        .buttonStyle(.bordered)
                        .controlSize(.small)
                    }

                    Spacer()

                    Button(role: .destructive) {
                        imageToDelete = image
                    } label: {
                        Label("Delete", systemImage: "trash")
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.small)
                }
                .padding(.horizontal, 12)
                .padding(.bottom, 4)

                ScrollView(.vertical, showsIndicators: false) {
                    VStack(alignment: .leading, spacing: 4) {
                        detailRow("Image file", value: image.imageFileName)
                        detailRow("Location", value: image.directoryURL.path)
                        if let label = image.imageLabel {
                            detailRow("Source", value: label)
                        }
                        if let total = manager.totalSizeForImage(image) {
                            let f = ByteCountFormatter()
                            detailRow("Total size",
                                      value: f.string(fromByteCount: total))
                        }
                        if let modified = image.fileModificationDate {
                            detailRow("Last modified",
                                      value: formatDate(modified))
                        }
                        detailRow("Added", value: formatDate(image.addedAt))
                        if let launched = image.lastLaunchedAt {
                            detailRow("Last launched",
                                      value: formatDate(launched))
                        }
                    }
                    .padding(.horizontal, 12)
                    .padding(.vertical, 4)
                }
                .frame(maxHeight: 120)
            } else {
                Text("Tap an image to see details, or long-press for options.")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 8)
            }
        }
        .background(Color(.secondarySystemBackground))
    }

    private func detailRow(_ label: String, value: String) -> some View {
        HStack(alignment: .top) {
            Text(label)
                .font(.caption)
                .foregroundColor(.secondary)
                .frame(width: 110, alignment: .leading)
            Text(value)
                .font(.caption)
        }
    }

    private func formatDate(_ date: Date) -> String {
        let formatter = DateFormatter()
        formatter.dateStyle = .medium
        formatter.timeStyle = .short
        return formatter.string(from: date)
    }

    // MARK: - Error banner

    @ViewBuilder
    private var errorBanner: some View {
        if let msg = manager.errorMessage {
            HStack {
                Image(systemName: "exclamationmark.triangle.fill")
                    .foregroundColor(.yellow)
                Text(msg).font(.caption)
            }
            .padding()
            .background(.ultraThinMaterial)
            .cornerRadius(10)
            .padding()
            .onTapGesture { manager.errorMessage = nil }
        }
    }

    // MARK: - Actions

    private func launch(_ image: St80Image) {
        manager.markLaunched(image)
        onLaunch(image)
    }

    private func showInFiles(_ image: St80Image) {
        let url = image.directoryURL
        #if targetEnvironment(macCatalyst)
        UIApplication.shared.open(url)
        #else
        guard let scene = UIApplication.shared.connectedScenes.first
                as? UIWindowScene,
              let rootVC = scene.windows.first?.rootViewController else { return }
        let picker = UIDocumentPickerViewController(
            forOpeningContentTypes: [.item, .folder])
        picker.directoryURL = url
        picker.allowsMultipleSelection = false
        rootVC.present(picker, animated: true)
        #endif
    }
}

// MARK: - Share sheet wrapper

struct ShareSheet: UIViewControllerRepresentable {
    let activityItems: [Any]

    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: activityItems,
                                 applicationActivities: nil)
    }

    func updateUIViewController(_ vc: UIActivityViewController,
                                context: Context) {}
}
