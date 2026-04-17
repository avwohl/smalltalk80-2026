# Mac Smalltalk80 — Remaining Work (excl. JIT)

Unordered. Pick anything.

1. **Cursor sync on Catalyst.** `AppleHal.cpp` tracks Smalltalk's
   `set_cursor_image` but the Catalyst Swift side never reads it, so
   the system pointer stays as the arrow even when the image changes
   it (text I-beam in editors, etc.). The pure AppKit target already
   does this in `St80MTKView` — port the same path to
   `app/apple-catalyst/St80MTKView.swift`.

2. **Window resize.** `MetalRenderer.drawableSizeWillChange` is
   empty. Resizing the window stretches the 1-bit display instead of
   asking the VM for a new display size. Wire a resize path, or at
   least lock the aspect ratio.

3. **System clipboard bridge.** Smalltalk's cut/copy/paste is
   internal only. Typing in the Mac app can't paste from a browser,
   and text copied inside Smalltalk doesn't appear on the Mac
   clipboard. Bridge via `UIPasteboard` on Catalyst and the relevant
   primitives on the core side.

4. **macOS menu bar.** No File/Edit/View menus and no "About
   Smalltalk80" menu item — the About sheet only shows from the
   in-app info button. Hook `UIMenuBuilder` on Catalyst to publish a
   standard menu bar and wire the About menu item to present
   `AboutView`.

5. **Snapshot "Save As".** Image snapshot only writes to the
   sandboxed Documents dir. Add a user-facing "save image to disk"
   flow using `UIDocumentPickerViewController` so users can export
   snapshots outside the sandbox.

6. **Signing / notarization / release packaging.** Phase-5 item.
   Needed for distribution outside Xcode. Configure Developer ID
   signing, notarization, and a DMG build in `scripts/`.

7. **Phase-5 interpreter audit.** Cache OOP→address on the hot
   path; add monomorphic inline-cache at send sites. Sets up the
   JIT tier-up seams while also making the interpreter faster.

8. **Find / document correctness tests for the VM + image.** We
   pass trace2/trace3 byte-for-byte, but there's no checklist of
   what else to run. Inventory: dbanay's test vectors, BitBlt unit
   tests, primitive-table coverage, any in-image Smalltalk test
   suites we should be exercising.

9. **Find / document performance tests.** No established benchmark
   suite yet. Identify candidates — Blue Book tinyBenchmarks,
   macroBenchmarks from Squeak ported to the Blue Book image,
   BitBlt throughput — and wire them so we can compare interpreter
   vs JIT once that lands.

10. **Port `validate_smalltalk_image` functionality.** Sibling project
    `../validate_smalltalk_image` has image-checking utilities. Port at
    least the **check** and **diff/sha** commands into a tool in this
    repo (likely `tools/st80_validate` alongside `st80_run` and
    `st80_probe`). Useful for sanity-checking user-provided images and
    comparing snapshots across runs.
