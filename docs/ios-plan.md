# iOS Plan

Phase-3 target per `plan.md`: run Smalltalk80 on iPad (and iPhone as
follow-up) using the same shared C++ core + Swift UI we use for Mac
Catalyst. Interpreter-only on iOS — JIT stays off because the App
Store won't grant the entitlement.

For Mac-specific remaining work see `todo-mac.md`.
For the core-VM Phase-5 work (shared with Mac) see
`interpreter-audit.md`.

## Status today

The Catalyst build already compiles the Swift layer against a
`macabi` target that is UIKit-based — most of the code is the same
source we'd ship on iOS. Existing plumbing we inherit:

- All UI views (`ImageLibraryView`, `AboutView`, `DocumentExporter`,
  `ContentView`) are pure SwiftUI + UIKit and already iOS-compatible.
- `St80Image` + `ImageManager` store images in the app's sandboxed
  Documents dir, which is where iOS wants them.
- `St80MTKView`'s touch plumbing already uses `touchesBegan/Moved/Ended`
  with a red-button default.
- `AboutView.swift`'s NSCursor / NSEvent bridges are
  `#if targetEnvironment(macCatalyst)` so they compile away on iOS.
- `xcframework` already has an `ios-arm64` slice built by
  `scripts/build-xcframework.sh`.

What doesn't work today on iOS:

- No iOS target in `st80.xcodeproj` yet. The xcodeproj currently
  builds one target (Catalyst-only). We need a separate iOS target
  that uses the `ios-arm64` xcframework slice.
- No input mapping for yellow / blue. On Mac we use right-click /
  middle-click via `UIContextMenuInteraction` / the `NSEvent`
  bridge. iOS has neither.
- No soft-keyboard handling. `pressesBegan` only covers hardware
  keyboards; iPad on-screen keyboard needs `UIKeyInput`.
- No touch-centric gestures for menus, window management, or
  scrolling.
- No iPad vs iPhone adaptive layout — the library view is desktop-y.
- No `CFBundleDocumentTypes` wiring for `.image` file opening from
  Files.app.

## Phased plan

### Phase 3.0 — Target + boot (1 day)

- Add an `st80-ios` target to `st80.xcodeproj` that picks the
  `ios-arm64` xcframework slice.
- `LSRequiresIPhoneOS = YES`, device family = iPad (iPhone later).
- Adjust minimum-deployment to iOS 15 (SwiftUI features) / 16
  (UIKeyInput modern additions). Catalyst already targets 13.0;
  iOS can be later since we don't need to support old devices.
- Run on simulator + device, confirm image loads and the VM screen
  draws. Most of the work here is project-file plumbing; the Swift
  and C++ already build.

### Phase 3.1 — Touch → 3-button mapping (1 day)

Blue Book's three buttons via finger gestures. Lift iospharo's
conventions since they're idiomatic:

- **Single tap / drag** — red. Via existing `touchesBegan/Moved/Ended`.
- **Long-press (0.5 s)** — yellow. `UILongPressGestureRecognizer`
  on the MTKView; `.began` fires yellow-down, `.changed` tracks for
  drag-to-pick menus, `.ended` fires yellow-up. Apply the same
  sticky-menu state machine used on Catalyst (touchesBegan commits
  the pending menu).
- **Two-finger tap** — blue. `UITapGestureRecognizer` with
  `numberOfTouchesRequired = 2`. Blue-down + sticky; next touch
  commits.
- **Two-finger drag** — pass-through (ignore). Reserve for future
  scroll / zoom.

Risk: `touchesBegan` fires for each finger in a multi-touch. Need
`cancelsTouchesInView = true` on the two-finger recognizers (per
iospharo's comment — "otherwise touchesBegan fires red-down for
each finger").

### Phase 3.2 — Soft keyboard (1–2 days)

iPad users without a hardware keyboard expect the on-screen keyboard
when focus is in a text pane.

- Make `St80MTKView` conform to `UIKeyInput` (only on non-Catalyst).
- `hasText = true`, `insertText(_:)` posts each character via
  `st80_post_key_down`, `deleteBackward()` posts BS.
- `becomeFirstResponder()` when the user double-taps (convention: a
  deliberate gesture to summon the keyboard; hidden otherwise).
- Provide a way to dismiss (existing iOS keyboard bar button or a
  swipe-down gesture).
- Disable autocorrect / smart quotes / predictive — Smalltalk-80
  wants raw ASCII, not "smart" text. See iospharo's
  `PharoMTKView: UIKeyInput` for the pattern (same disables apply).

Known pain: iPad floating keyboard. iospharo hit subtle layout bugs
where the keyboard would re-fire keyDown events after layout
settled; comments in their `PharoCanvasView.swift` around layout
freezing describe the workaround. Expect to need similar discipline.

### Phase 3.2.5 — Control strip (0.5–1 day)

The iOS soft keyboard is missing the keys Smalltalk actually needs —
Ctrl, Cmd (for desktop-style shortcuts), Esc, Tab, arrows. Port
iospharo's vertical control strip as a left-edge overlay. It's
MIT (same author), so direct port + attribution in
`THIRD_PARTY_LICENSES`.

Buttons we need, in priority order:

1. **Ctrl toggle** — one-shot modifier. Tap to arm, next key press
   consumes it. Symbol `control`. State stored in a view-model
   observable so `insertText` / `pressesBegan` can read it.
2. **Cmd toggle** — same pattern. Symbol `command`.
3. **Esc** — sends ASCII 27. No modifier.
4. **Tab** — sends 9. Useful for browser pane-switching.
5. **Backspace** — sends 8. The soft keyboard has one but the strip
   copy stays accessible when the keyboard is hidden.
6. **Keyboard toggle** — show/hide the soft keyboard. Saves a corner
   of screen real estate when not typing.

Source to mirror: `iospharo/iospharo/App/ContentView.swift` —
`StripButton` view (drop in near-verbatim), the `ctrlModifierActive`
/ `cmdModifierActive` published state on `PharoBridge` (we'd add
equivalent on a new `St80KeyboardState` observable, not on the C
bridge), and the vertical `VStack` layout in the body.

Blue Book-specific trim: drop the DoIt / PrintIt / InspectIt / Debug
buttons — those are Pharo conventions. Add a Yellow-button / Blue-
button shortcut instead (tap either while a touch is active to
force that menu, bypassing long-press / two-finger).

### Phase 3.2.6 — Strip geometry (half-day)

Place the strip on the leading edge of the screen in landscape,
respecting device corners / Dynamic Island / notch / home indicator.
`../claude-skills/skills/device-geometry.md` has the authoritative
squircle formula (n=5 superellipse) and a worked table of R / SA
values per device. Do **not** hand-pick padding constants — use the
formula at runtime so layout stays correct on every iPhone model.

Required runtime work:

1. Read `window.safeAreaInsets.leading` + `.bottom` from the hosted
   `UIWindow` to derive display corner radius R and home-indicator
   inset. Lookup table in the skill doc maps SA_leading → R.
2. For each strip button stacked from the top or bottom corner,
   compute `y_min = R - (R^5 - (R - button_left_x)^5)^(1/5)`
   where `button_left_x = (stripWidth - buttonSize) / 2`. Add
   ~2 pt visual breathing room.
3. On Dynamic Island iPhones in landscape, the DI occupies ~127 pt
   vertically at the leading edge centre. Split the strip into
   "above DI" and "below DI" zones; place Ctrl/Cmd/keyboard-toggle
   in the top zone, action buttons in the bottom zone, centred in
   their respective zones.
4. On iPads (R=18) the corner intrusion is small (<2 pt at x=4);
   a static 28 pt top padding clears the Smalltalk menu bar + the
   corner.

Reference implementation: the `iPhoneTopPadding` /
`iPhoneBottomPadding` / `squircleIntrusion` computed properties in
iospharo's `ContentView.swift` (roughly lines 400–505). Port
verbatim — the geometry math isn't Pharo-specific.

### Phase 3.3 — Adaptive layout (0.5 day)

- Library view: keep the SwiftUI List; it adapts to iPad fine as-is.
- Once an image is launched, the VM display fills the screen with
  our existing letterbox path. The VM's 640×480 / 800×600 Xerox
  display is smaller than an iPad Pro's screen, so letterbox gives
  honest pixels at 1:1 rather than a blurry upscale.
- Portrait / landscape: pin orientation to landscape initially. The
  Xerox display is wider than tall; portrait would waste pixels.
- iPhone: defer to Phase 3.5. The display just doesn't fit — at
  390pt wide, a 640-wide VM display letterboxed would be ~60% screen
  height. Plausible but cramped; worth doing after iPad is solid.

### Phase 3.4 — File handling + state (0.5 day)

- `CFBundleDocumentTypes` entry for `public.data` with conforming
  types `com.awohl.st80.image` so iPad users can "Open In…" from
  Files.app.
- Already have `ImageManager.importImage` — wire `application
  (_:open:options:)` through to it.
- `UIDocumentPickerViewController` instead of the Catalyst-style
  SwiftUI `.fileImporter` when needed; actually `.fileImporter` is
  fine on iOS too — keep what we have.

### Phase 3.5 — iPhone fit & finish (open)

Post-iPad. Requires decisions about:
- Scale the VM display or letterbox at small size?
- Render the library UI in a full-screen nav stack vs a sheet?
- Keyboard UX on a phone screen — does the soft keyboard cover half
  the VM display when summoned?

## Entitlements + App Store

- Standard sandboxed-app entitlement; nothing exotic required.
- No JIT entitlement (we don't need it — interpreter only on iOS).
- No camera / microphone / location / network. We just read files
  from the sandbox and the user-picked Documents dir.
- App Store metadata: screenshots, description, "Xerox Smalltalk-80
  image sold separately" style disclaimer pointing at Wolczko's
  redistribution URL.
- Export compliance: no crypto (we don't bridge SSL). File a
  "no cryptography" declaration.

## What we won't do

- **JIT.** Apple won't grant the entitlement for general-purpose
  distribution.
- **Mouse / trackpad optimization on iPad.** The code that handles
  Mac right-click via `UIContextMenuInteraction` doesn't hurt iPad
  (it runs when a pointer is connected), but we design the primary
  gesture set for touch.
- **iOS-only features the Mac doesn't get.** Keep the feature set
  equivalent so users on different Apple platforms see the same
  image behavior.

## Open questions

- Do we want to package both `.ipa` and Catalyst `.app` from the
  same archive action? Xcode can do this with a combined scheme;
  worth validating early.
- Does the Xerox v2 image's fixed 640×480 display need a "pixel
  doubling" mode on iPad Pro where the letterbox is distractingly
  large? Integer scale would be sharp but smaller; fractional scale
  blurs the 1-bit bitmap.
- Testflight distribution for early feedback vs App Store direct.
  Testflight is lower-ceremony.
