# st80-2026 — Mac Catalyst + iOS (Xcode)

Mac Catalyst frontend (UIKit + SwiftUI + Metal). Same C++ core as
the AppKit app, same Swift view patterns, but a UIKit-shaped host
so the identical target can also ship as an iPhone / iPad app.
This directory also drives the Xcode project used for App Store
submission.

## Prerequisites

Same as the AppKit build — see [`../apple/README.md`](../apple/README.md)
for Xcode.app, `cmake`, `gh`.

## Catalyst app (CMake-driven, for local development)

`app/apple-catalyst/build-catalyst-app.sh` builds a Catalyst `.app`
without needing to open Xcode. It's the fastest edit-compile-run
loop on Apple.

    app/apple-catalyst/build-catalyst-app.sh
    open build/St80Catalyst.app \
        --args "$PWD/reference/xerox-image/VirtualImage"

`otool -l` confirms the Catalyst binary carries `platform 6`
(macCatalyst) while the AppKit binary is `platform 1` (macOS).
Both render the identical Xerox desktop.

The in-app launcher (see `ImageLibraryView.swift`) handles
downloading and importing images from the GUI; passing an image
path on the command line just skips the launcher for this one
run.

## Xcode project (for App Store submission)

`st80.xcodeproj` at the repo root is the proper Xcode project for
producing a signed archive uploadable to App Store Connect.

One-time setup:

    cp Local.xcconfig.example Local.xcconfig
    # edit Local.xcconfig → set DEVELOPMENT_TEAM = XXXXXXXXXX

Build from the command line:

    xcodebuild -project st80.xcodeproj -scheme St80 \
        -configuration Release \
        -destination 'platform=macOS,variant=Mac Catalyst' \
        -derivedDataPath build-xcodeproj \
        build

The project supports `SUPPORTS_MACCATALYST = YES` and
`TARGETED_DEVICE_FAMILY = "1,2,6"` — the same target can build for
iPhone, iPad, and Mac Catalyst.

For App Store submission: open in Xcode, `Product → Archive`, then
upload via the Organizer.

A `Check XCFramework Freshness` build phase regenerates
`Frameworks/St80Core.xcframework` (via
`scripts/build-xcframework.sh`) whenever a VM source file is newer
than the xcframework's `Info.plist`, so editing C++ core code and
hitting build in Xcode Just Works.
