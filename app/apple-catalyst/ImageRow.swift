// st80-2026 — ImageRow.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Table-style row in the image library. Columns mirror the header in
// ImageLibraryView. Adapted from
// ../iospharo/iospharo/Views/ImageRow.swift, with the Pharo-version
// column repurposed as a "Source" column showing the optional
// imageLabel.

import SwiftUI

struct ImageRow: View {
    let image: St80Image
    let isSelected: Bool
    var isAutoLaunch: Bool = false

    var body: some View {
        HStack(spacing: 0) {
            HStack(spacing: 4) {
                if isAutoLaunch {
                    Image(systemName: "star.fill")
                        .font(.system(size: 10))
                        .foregroundColor(.orange)
                }
                Text(image.name)
                    .font(.system(.body, design: .default))
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            Text(image.sourceLabel)
                .font(.system(.body, design: .default))
                .foregroundColor(.secondary)
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(width: ImageTableLayout.sourceWidth, alignment: .leading)

            Text(image.formattedSize ?? "—")
                .font(.system(.body, design: .default))
                .foregroundColor(.secondary)
                .frame(width: ImageTableLayout.sizeWidth, alignment: .trailing)

            Text(lastModifiedText)
                .font(.system(.body, design: .default))
                .foregroundColor(.secondary)
                .frame(width: ImageTableLayout.lastModifiedWidth, alignment: .leading)
                .padding(.leading, 12)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(isSelected ? Color.accentColor.opacity(0.15) : Color.clear)
        .contentShape(Rectangle())
    }

    private var lastModifiedText: String {
        if let date = image.fileModificationDate {
            let formatter = RelativeDateTimeFormatter()
            formatter.unitsStyle = .full
            return formatter.localizedString(for: date, relativeTo: Date())
        }
        return "—"
    }
}

enum ImageTableLayout {
    static let sourceWidth: CGFloat = 180
    static let sizeWidth: CGFloat = 80
    static let lastModifiedWidth: CGFloat = 140
}
