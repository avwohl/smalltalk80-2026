// st80-2026 — ImageLibraryView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// First-launch UI. Empty state offers a "Download Xerox 1983 image"
// button; once downloaded the library lists the image with a Launch
// button. Delete via swipe.

import SwiftUI
import UniformTypeIdentifiers

struct ImageLibraryView: View {

    @ObservedObject var manager: ImageManager
    let onLaunch: (St80Image) -> Void

    @State private var showingAbout = false
    @State private var showingImporter = false

    var body: some View {
        VStack(spacing: 24) {
            topBar
            header

            if manager.images.isEmpty {
                emptyState
            } else {
                libraryList
            }

            if let msg = manager.errorMessage {
                Text(msg)
                    .foregroundColor(.red)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal)
            }

            if manager.isDownloading {
                progressRow
            }

            Spacer(minLength: 0)
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .onAppear { manager.load() }
        .sheet(isPresented: $showingAbout) { AboutView() }
        .fileImporter(isPresented: $showingImporter,
                      allowedContentTypes: [.data],
                      allowsMultipleSelection: false) { result in
            if case .success(let urls) = result, let url = urls.first {
                manager.importImage(from: url)
            } else if case .failure(let err) = result {
                manager.errorMessage = "Import failed: \(err.localizedDescription)"
            }
        }
    }

    private var topBar: some View {
        HStack {
            Button {
                showingImporter = true
            } label: {
                Image(systemName: "plus.circle")
                    .font(.title2)
            }
            .accessibilityLabel("Add image from file")

            Spacer()

            Button {
                showingAbout = true
            } label: {
                Image(systemName: "info.circle")
                    .font(.title2)
            }
            .accessibilityLabel("About")
        }
    }

    private var header: some View {
        VStack(spacing: 4) {
            Text("Smalltalk-80")
                .font(.largeTitle)
                .fontWeight(.bold)
            Text("1983 Xerox virtual image")
                .font(.subheadline)
                .foregroundColor(.secondary)
        }
        .padding(.top, 24)
    }

    private var emptyState: some View {
        VStack(spacing: 18) {
            Image(systemName: "square.stack.3d.up")
                .font(.system(size: 60))
                .foregroundColor(.secondary)

            Text("No image downloaded yet")
                .font(.title3)

            Text("Tap below to fetch the 1983 Xerox distribution.")
                .font(.subheadline)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)

            ForEach(ImageManager.Template.builtIn) { template in
                Button {
                    manager.downloadTemplate(template)
                } label: {
                    Label(template.label, systemImage: "arrow.down.circle.fill")
                        .font(.headline)
                        .padding(.horizontal, 18)
                        .padding(.vertical, 10)
                }
                .buttonStyle(.borderedProminent)
                .disabled(manager.isDownloading)
            }

            Button {
                showingImporter = true
            } label: {
                Label("Add image from file…", systemImage: "folder.badge.plus")
                    .font(.subheadline)
            }
            .buttonStyle(.bordered)
            .padding(.top, 6)
        }
        .padding(.top, 12)
    }

    private var libraryList: some View {
        List {
            ForEach(manager.images) { image in
                HStack(alignment: .center, spacing: 12) {
                    Image(systemName: "doc")
                        .foregroundColor(.secondary)
                    VStack(alignment: .leading, spacing: 2) {
                        Text(image.name).font(.headline)
                        Text(image.imageFileName)
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    Spacer()
                    Button("Launch") { onLaunch(image) }
                        .buttonStyle(.borderedProminent)
                }
                .swipeActions {
                    Button(role: .destructive) {
                        manager.deleteImage(image)
                    } label: {
                        Label("Delete", systemImage: "trash")
                    }
                }
            }
        }
        .listStyle(.plain)
    }

    private var progressRow: some View {
        VStack(spacing: 6) {
            ProgressView(value: manager.downloadProgress)
            if let status = manager.statusMessage {
                Text(status)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.horizontal)
    }
}
