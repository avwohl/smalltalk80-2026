// st80-2026 — AutoLaunchSplashView.swift (Catalyst / iOS)
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// 3-second countdown shown before auto-launching a starred image.
// Gives the user an escape hatch to cancel and return to the library
// (e.g. if the image is damaged or they want to pick a different one).
// Adapted from ../iospharo/iospharo/Views/AutoLaunchSplashView.swift.

import SwiftUI

struct AutoLaunchSplashView: View {
    let imageName: String
    let onLaunch: () -> Void
    let onCancel: () -> Void

    @State private var countdown: Int = 3

    private let timer = Timer.publish(every: 1, on: .main, in: .common)
        .autoconnect()

    var body: some View {
        VStack(spacing: 24) {
            Spacer()

            Image(systemName: "square.stack.3d.up.fill")
                .font(.system(size: 56))
                .foregroundColor(.accentColor)

            Text("Auto-launching")
                .font(.title2)
                .foregroundColor(.primary)

            Text(imageName)
                .font(.headline)
                .foregroundColor(.secondary)

            Text("\(countdown)")
                .font(.system(size: 48, weight: .bold, design: .rounded))
                .foregroundColor(.accentColor)

            Button {
                onCancel()
            } label: {
                Label("Show Library", systemImage: "list.bullet")
                    .font(.headline)
                    .frame(maxWidth: 280)
            }
            .buttonStyle(.bordered)
            .controlSize(.large)

            Spacer()
        }
        .padding()
        .onReceive(timer) { _ in
            if countdown > 1 {
                countdown -= 1
            } else {
                onLaunch()
            }
        }
    }
}
