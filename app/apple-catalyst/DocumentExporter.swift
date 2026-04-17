// st80-2026 — DocumentExporter.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// SwiftUI wrapper around UIDocumentPickerViewController in "export"
// mode. The picker lets the user pick a destination outside the app's
// sandbox and copies the current Smalltalk image there. We use copy
// semantics (`asCopy: true`) so the sandboxed original stays intact.

import SwiftUI
import UIKit

struct DocumentExporter: UIViewControllerRepresentable {

    let url: URL
    let onDismiss: () -> Void

    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(
            forExporting: [url], asCopy: true)
        picker.delegate = context.coordinator
        picker.shouldShowFileExtensions = true
        return picker
    }

    func updateUIViewController(_ c: UIDocumentPickerViewController,
                                context: Context) {}

    func makeCoordinator() -> Coordinator { Coordinator(self) }

    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let parent: DocumentExporter
        init(_ parent: DocumentExporter) { self.parent = parent }

        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
            parent.onDismiss()
        }

        func documentPicker(_ controller: UIDocumentPickerViewController,
                            didPickDocumentsAt urls: [URL]) {
            parent.onDismiss()
        }
    }
}
