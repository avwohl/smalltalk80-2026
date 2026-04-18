// st80-2026 — NewImageView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Sheet that lets the user pick a built-in template to download or
// supply a custom URL. Adapted from
// ../iospharo/iospharo/Views/NewImageView.swift.

import SwiftUI

struct NewImageView: View {

    @ObservedObject var manager: ImageManager
    @Environment(\.dismiss) private var dismiss

    @State private var customURL: String = ""
    @State private var showingCustomURL = false

    var body: some View {
        NavigationView {
            List {
                Section("Built-in") {
                    ForEach(ImageManager.Template.builtIn) { template in
                        Button {
                            manager.downloadTemplate(template)
                            dismiss()
                        } label: {
                            HStack {
                                Image(systemName: "arrow.down.circle")
                                    .foregroundColor(.blue)
                                    .frame(width: 30)
                                VStack(alignment: .leading) {
                                    Text(template.label)
                                        .foregroundColor(.primary)
                                    Text(template.assetNames.joined(separator: ", "))
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }
                                Spacer()
                            }
                        }
                        .disabled(manager.isDownloading)
                    }
                }

                Section("Custom") {
                    if showingCustomURL {
                        VStack(spacing: 12) {
                            TextField("https://example.com/VirtualImage",
                                      text: $customURL)
                                .textContentType(.URL)
                                .keyboardType(.URL)
                                .textInputAutocapitalization(.never)
                                .disableAutocorrection(true)

                            Button("Download") {
                                let trimmed = customURL.trimmingCharacters(
                                    in: .whitespacesAndNewlines)
                                guard let url = URL(string: trimmed),
                                      !trimmed.isEmpty else { return }
                                manager.downloadCustomURL(url)
                                dismiss()
                            }
                            .disabled(customURL.isEmpty || manager.isDownloading)
                        }
                    } else {
                        Button {
                            showingCustomURL = true
                        } label: {
                            HStack {
                                Image(systemName: "link")
                                    .foregroundColor(.blue)
                                    .frame(width: 30)
                                Text("Download from URL…")
                                    .foregroundColor(.primary)
                            }
                        }
                    }
                }

                Section {
                    Text("Custom URLs should point at a single image file. "
                         + "Use “Add image from file…” afterward to attach "
                         + "companion sources/changes files from disk.")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            .navigationTitle("Download Image")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button { dismiss() } label: {
                        #if targetEnvironment(macCatalyst)
                        Image(systemName: "xmark.circle.fill")
                            .foregroundStyle(.secondary)
                        #else
                        Text("Cancel")
                        #endif
                    }
                }
            }
        }
    }
}
