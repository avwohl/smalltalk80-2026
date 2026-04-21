# Release — signing, notarization, distribution

Runbook for shipping a distributable `.dmg` of the Mac Catalyst app.
Requires an Apple Developer Program membership; most of this can't be
automated without the user's credentials.

Other targets have their own release paths and don't belong here:

  * Windows — NSIS `.exe`, WiX `.msi`, and MSIX for the Microsoft
    Store. See [`../app/windows/README.md`](../app/windows/README.md)
    "Release build + installer packaging".
  * Linux — `.deb` / `.rpm` via CPack, plus a GitHub Actions
    workflow that attaches x86_64 + aarch64 artifacts to tagged
    releases. See [`../app/linux/README.md`](../app/linux/README.md)
    and `.github/workflows/release-linux.yml`.

## Prerequisites (one-time, per-developer)

- Apple Developer Program membership ($99/yr). Required for the
  Developer ID certificate, notarization, and later App Store
  distribution.
- Xcode is signed in under the target Apple ID (Settings → Accounts).
- A **Developer ID Application** certificate in the login keychain —
  this is the one Apple issues for notarized distribution *outside*
  the App Store. Create from Xcode → Settings → Accounts → Manage
  Certificates → "+" → Developer ID Application.
- An **app-specific password** for `notarytool`, or a stored keychain
  profile (`xcrun notarytool store-credentials`).

If the app will be sandboxed outside the Mac App Store it still needs
`com.apple.security.app-sandbox` in the entitlements file; we already
have that.

## Code signing

The Catalyst build currently uses `CODE_SIGN_STYLE = Automatic` +
`CODE_SIGN_IDENTITY[sdk=macosx*] = Apple Development` (see
`st80.xcodeproj/project.pbxproj`). That's fine for local debug runs
but won't pass notarization. For release:

    xcodebuild -project st80.xcodeproj \
               -scheme st80-2026 \
               -configuration Release \
               -destination 'platform=macOS,variant=Mac Catalyst' \
               CODE_SIGN_IDENTITY="Developer ID Application: <Team Name>" \
               CODE_SIGN_STYLE=Manual \
               DEVELOPMENT_TEAM=<TEAMID> \
               build

The unattended `swiftc`-driven `app/apple-catalyst/build-catalyst-app.sh`
produces an unsigned `.app` suitable for testing but not release —
don't try to notarize its output.

## Notarization

After building with the Developer ID identity above:

    xcrun notarytool submit build/Build/Products/Release-maccatalyst/st80-2026.app.zip \
        --keychain-profile "st80-notary" \
        --wait

Zip first — `notarytool` only accepts `.zip`, `.pkg`, `.dmg`.

Staple the ticket once approved:

    xcrun stapler staple build/.../st80-2026.app

Verify:

    spctl --assess --type execute --verbose build/.../st80-2026.app
    codesign --verify --deep --strict --verbose=2 build/.../st80-2026.app

## Packaging

`.dmg` with a background image and drag-to-Applications target. The
minimal route:

    hdiutil create -volname "Smalltalk80" \
                   -srcfolder build/.../st80-2026.app \
                   -ov -format UDZO \
                   dist/Smalltalk80-<version>.dmg

For something prettier, use `create-dmg` from homebrew.

The `.dmg` also needs signing + stapling:

    codesign --sign "Developer ID Application: <Team>" \
             --options runtime \
             --timestamp \
             dist/Smalltalk80-<version>.dmg
    xcrun notarytool submit dist/... --wait
    xcrun stapler staple dist/...

## What's blocking automation right now

- Developer ID credentials are per-user secrets; they can't live in
  the repo.
- `DEVELOPMENT_TEAM` also isn't public — goes in `Local.xcconfig`
  (already gitignored).
- `notarytool store-credentials` is an interactive step (requires
  the app-specific password).

Once those are present on a machine, a `scripts/release.sh` that runs
the sequence above end-to-end is ~50 lines of shell. Not written yet
because it can't be tested without the credentials. A stub with the
commands above is the right starting point; `gh release` upload is
the last step.

## Audit trail

Record the notarization ticket ID and submission UUID with each
release — Apple can revoke a notarization and we need a way to
correlate user reports with builds. `notarytool history` shows the
list.
