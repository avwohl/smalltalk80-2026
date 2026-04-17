# Mac Smalltalk80 — Remaining Work (excl. JIT & iOS)

Mac Catalyst-only punch list. iOS-specific work lives in
`docs/ios-plan.md`; the shared C++ core is in Phase-5 territory
tracked via `docs/interpreter-audit.md`.

Rewritten 2026-04-17 after the polish sweep. Items 1–5 and 10–12 from
the first pass shipped; what's left is below.

## Code work that would most help users

1. **Save-on-close prompt.** Today a plain Mac Cmd-Q or window-close
   kills the app without offering to snapshot. Intercept it, check
   whether the image has unsaved changes (Smalltalk's `Smalltalk
   isDirty` or a primitive we add), and ask the user before exit.
   Blue Book convention is "quit = discard", but that's a footgun on
   macOS; a polite "save first?" is expected.

2. **File associations.** Register `VirtualImage` / `.image` files so
   double-clicking one in Finder launches Smalltalk80 and loads it.
   Info.plist `UTImportedTypeDeclarations` + `CFBundleDocumentTypes`.
   Wire `application(_:open:)` to route the URL through
   `ImageManager.importImage` (code already exists).

3. **State restoration.** Remember which image was last launched and
   resume automatically on next start, skipping the library screen.
   NSUserDefaults + `launchedImagePath`.

4. **Verify cursor overlay in all states.** The overlay now tracks
   hover AND click-drag, but needs interactive verification:
   - text-selection drag shows the chevron following the cursor
   - scrollbar regions show three distinct shapes
   - menu/desktop shows the default Smalltalk arrow, not the macOS
     system pointer
   All three worked in first tests with caveats. Any remaining "no
   cursor visible" moments probably indicate the image has set an
   unusual form we render as blank.

## Phase-5 interpreter audit — implement the proposals

`docs/interpreter-audit.md` spells out the ranked plan. Highest-value
items:

5. **Measure method-cache hit rate.** ~20 lines. Hit-rate counter in
   the send path, dumped at `st80_run` exit. Informs whether to
   invest in a larger cache vs PIC.

6. **Cache the current method's byte pointer.** Estimated 1.5–2×
   interpreter speedup. `fetchByte` currently re-walks the Object
   Table on every bytecode. Biggest single win short of JIT.

7. **SmallInteger specialization for arithmetic bytecodes** (176–207).
   1.2–1.5× on arithmetic-heavy code. Same safety net: `trace2_check`
   still passes.

## Release-machinery items

8. **Actually sign and notarize.** Runbook in `docs/release.md`.
   Requires the user's Developer ID certificate and app-specific
   password; can't be done in isolation. Once creds are present,
   a `scripts/release.sh` that runs the sequence is maybe 50 lines.

## Testing & benchmarks — wire what we documented

9. **Wire `trace3` regression check** alongside `trace2_check.sh`.
   Trace3 exercises message-send boundaries that trace2 doesn't.

10. **Add `tinyBenchmarks` driver** in `tools/`. Boots the image,
    drives `BenchmarksReport run`, captures the transcript, parses
    the numeric pair. Gives us the single headline number interp vs
    JIT will compare on.

11. **Add `st80_validate check` to CTest.** Tool already written
    (item 10 in the prior pass); just needs a CTest target that
    runs it on the Xerox image after boot.

## Things explicitly parked

- iOS target (Phase 3).
- JIT (Phase 6).
- Windows / Linux (Phase 4).
- Copy-out clipboard (requires HAL primitive + image mod).
- VoiceOver / accessibility audit (bitmap content has no meaningful
  semantics to expose).
