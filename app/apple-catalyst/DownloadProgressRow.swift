// st80-2026 — DownloadProgressRow.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Inline progress indicator shown above the image list while a
// download is in flight, with a cancel button. Adapted from
// ../iospharo/iospharo/Views/DownloadProgressRow.swift.

import SwiftUI

struct DownloadProgressRow: View {
    @ObservedObject var manager: ImageManager

    var body: some View {
        HStack(spacing: 12) {
            ProgressView().frame(width: 36)

            VStack(alignment: .leading, spacing: 4) {
                Text(manager.statusMessage ?? "Downloading…")
                    .font(.body)
                ProgressView(value: manager.downloadProgress)
                    .progressViewStyle(.linear)
            }

            Button(role: .destructive) {
                manager.cancelDownload()
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .foregroundColor(.secondary)
            }
            .buttonStyle(.plain)
        }
        .padding(.vertical, 4)
    }
}
