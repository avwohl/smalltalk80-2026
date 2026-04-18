// st80-2026 — SettingsView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Settings sheet with app version, project links, and a navigation
// link to the Acknowledgements view. Adapted from
// ../iospharo/iospharo/Views/SettingsView.swift, minus the "VM
// Diagnostics" entry (st80 doesn't have an equivalent diagnostics
// surface yet).

import SwiftUI

struct SettingsView: View {

    @Environment(\.dismiss) private var dismiss

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
                Section("About") {
                    HStack {
                        Text("VM Version")
                        Spacer()
                        Text(version).foregroundColor(.secondary)
                    }

                    Link(destination: URL(string:
                        "https://github.com/avwohl/smalltalk80-2026")!) {
                        HStack {
                            Label("GitHub Project", systemImage: "link")
                            Spacer()
                            Image(systemName: "arrow.up.right.square")
                                .foregroundColor(.secondary)
                        }
                    }

                    Link(destination: URL(string:
                        "https://github.com/avwohl/smalltalk80-2026/issues")!) {
                        HStack {
                            Label("Report a Bug", systemImage: "ladybug")
                            Spacer()
                            Image(systemName: "arrow.up.right.square")
                                .foregroundColor(.secondary)
                        }
                    }

                    Link(destination: URL(string:
                        "https://github.com/avwohl/smalltalk80-2026/blob/main/docs/changes.md")!) {
                        HStack {
                            Label("Changes", systemImage: "list.bullet.rectangle")
                            Spacer()
                            Image(systemName: "arrow.up.right.square")
                                .foregroundColor(.secondary)
                        }
                    }

                    NavigationLink {
                        AcknowledgementsView()
                    } label: {
                        Label("Acknowledgements", systemImage: "heart")
                    }
                }

                Section {
                    Text("Implements the Smalltalk-80 virtual machine "
                         + "specified in Goldberg & Robson, “Smalltalk-80: "
                         + "The Language and its Implementation” "
                         + "(Addison-Wesley, 1983), chapters 26–30.")
                        .font(.footnote)
                        .foregroundColor(.secondary)
                }
            }
            .navigationTitle("Settings")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
        }
    }
}
