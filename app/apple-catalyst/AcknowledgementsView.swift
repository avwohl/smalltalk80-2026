// st80-2026 — AcknowledgementsView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Credits and license info for ported source. Mirrors
// THIRD_PARTY_LICENSES at the repo root. Adapted from
// ../iospharo/iospharo/Views/AcknowledgementsView.swift.

import SwiftUI

struct AcknowledgementsView: View {

    var body: some View {
        List {
            Section {
                Text("st80-2026 is built on the work of the Smalltalk-80 "
                     + "community and incorporates code ported from the "
                     + "open-source projects listed below. Each component "
                     + "retains its original license.")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
                    .padding(.vertical, 4)
            }

            section(
                name: "dbanay/Smalltalk",
                license: "MIT",
                copyright: "© 2020 Dan Banay",
                url: "https://github.com/dbanay/Smalltalk",
                description: "Primary C++ port source for object memory, "
                    + "BitBlt, interpreter dispatch, OOP encoding, and "
                    + "the image loader. Files containing ported code "
                    + "carry an in-source header naming the source path "
                    + "and commit they were derived from.")

            section(
                name: "Xerox Smalltalk-80 Virtual Image (v2, 1983)",
                license: "Xerox license — see image distribution",
                copyright: "© Xerox Corporation",
                url: "http://www.wolczko.com/st80/",
                description: "The 1983 v2 virtual image distributed by Mario "
                    + "Wolczko. Not redistributed with the app — fetched on "
                    + "first launch from the avwohl/st80-images companion "
                    + "repository, which mirrors Wolczko's distribution.")

            section(
                name: "Goldberg & Robson — Blue Book",
                license: "© Addison-Wesley, 1983",
                copyright: "© 1983 Adele Goldberg and David Robson",
                url: "https://en.wikipedia.org/wiki/Smalltalk-80%3A_The_Language_and_its_Implementation",
                description: "“Smalltalk-80: The Language and its "
                    + "Implementation.” Chapters 26–30 are the VM "
                    + "specification this implementation follows. "
                    + "Referenced as in-code citations only; no text is "
                    + "reproduced.")

            section(
                name: "iriyak/Smalltalk",
                license: "Reference only",
                copyright: "Various",
                url: "https://github.com/iriyak/Smalltalk",
                description: "Additional reference implementation of the "
                    + "Blue Book VM consulted during development.")

            section(
                name: "rochus-keller/Smalltalk",
                license: "Reference only — GPL, NOT linked",
                copyright: "Various",
                url: "https://github.com/rochus-keller/Smalltalk",
                description: "Read-only reference. Its image viewer is "
                    + "useful for inspecting the Xerox v2 image format. "
                    + "Its GPL license is incompatible with this project's "
                    + "MIT license, so none of its code is copied or "
                    + "linked here.")

            section(
                name: "SDL2",
                license: "zlib",
                copyright: "© 1997–2024 Sam Lantinga",
                url: "https://www.libsdl.org",
                description: "Cross-platform window/input layer used by the "
                    + "Linux frontend. The Catalyst/iOS frontend uses Metal "
                    + "directly and does not link SDL2.")
        }
        .navigationTitle("Acknowledgements")
        .navigationBarTitleDisplayMode(.inline)
    }

    @ViewBuilder
    private func section(name: String, license: String, copyright: String,
                         url: String, description: String) -> some View {
        Section(name) {
            Text(description).font(.subheadline)

            HStack {
                Text("License").foregroundColor(.secondary)
                Spacer()
                Text(license).multilineTextAlignment(.trailing)
            }
            .font(.subheadline)

            HStack(alignment: .top) {
                Text("Copyright").foregroundColor(.secondary)
                Spacer()
                Text(copyright).multilineTextAlignment(.trailing)
            }
            .font(.caption)

            if let link = URL(string: url) {
                Link(destination: link) {
                    HStack {
                        Text(url).font(.caption)
                        Spacer()
                        Image(systemName: "arrow.up.right.square")
                            .foregroundColor(.secondary)
                    }
                }
            }
        }
    }
}
