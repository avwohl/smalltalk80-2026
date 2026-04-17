// st80-2026 — AboutView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.

import SwiftUI

struct AboutView: View {

    @Environment(\.dismiss) private var dismiss

    private static let projectURL =
        URL(string: "https://github.com/avwohl/smalltalk80-2026")!

    private struct ReferenceRepo: Identifiable {
        let id = UUID()
        let name: String
        let url: URL
        let note: String
    }

    private static let references: [ReferenceRepo] = [
        ReferenceRepo(
            name: "dbanay/Smalltalk",
            url: URL(string: "https://github.com/dbanay/Smalltalk")!,
            note: "MIT — primary C++ port source for object memory, "
                + "BitBlt, interpreter dispatch, and primitives."),
        ReferenceRepo(
            name: "rochus-keller/Smalltalk",
            url: URL(string: "https://github.com/rochus-keller/Smalltalk")!,
            note: "GPL — read-only reference. Its image viewer is useful "
                + "for inspecting the Xerox v2 image format."),
        ReferenceRepo(
            name: "iriyak/Smalltalk",
            url: URL(string: "https://github.com/iriyak/Smalltalk")!,
            note: "Additional reference implementation of the Blue Book VM."),
    ]

    private var version: String {
        let short = Bundle.main.infoDictionary?["CFBundleShortVersionString"]
            as? String ?? "—"
        let build = Bundle.main.infoDictionary?["CFBundleVersion"]
            as? String ?? "—"
        return "\(short) (\(build))"
    }

    var body: some View {
        NavigationView {
            List {
                Section {
                    VStack(spacing: 6) {
                        Text("Smalltalk80")
                            .font(.largeTitle)
                            .fontWeight(.bold)
                        Text("Blue Book VM, 1983 Xerox virtual image")
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                        Text("Version \(version)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 8)
                }

                Section("Project") {
                    Link(destination: Self.projectURL) {
                        HStack {
                            Label("GitHub Repository", systemImage: "link")
                            Spacer()
                            Text("avwohl/smalltalk80-2026")
                                .font(.caption)
                                .foregroundColor(.secondary)
                            Image(systemName: "arrow.up.right.square")
                                .foregroundColor(.secondary)
                        }
                    }
                }

                Section("References") {
                    ForEach(Self.references) { repo in
                        VStack(alignment: .leading, spacing: 6) {
                            Link(destination: repo.url) {
                                HStack {
                                    Text(repo.name)
                                        .font(.headline)
                                    Spacer()
                                    Image(systemName: "arrow.up.right.square")
                                        .foregroundColor(.secondary)
                                }
                            }
                            Text(repo.note)
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                        .padding(.vertical, 2)
                    }
                }

                Section {
                    Text("Implements the Smalltalk-80 virtual machine as "
                         + "specified in Goldberg & Robson, \"Smalltalk-80: "
                         + "The Language and its Implementation\" "
                         + "(Addison-Wesley, 1983), chapters 26–30.")
                        .font(.footnote)
                        .foregroundColor(.secondary)
                }
            }
            .navigationTitle("About")
#if targetEnvironment(macCatalyst)
            .navigationBarTitleDisplayMode(.inline)
#else
            .navigationBarTitleDisplayMode(.inline)
#endif
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}
