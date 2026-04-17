# Claude Code Instructions — st80-2026

## Project Context

st80-2026 is a Smalltalk-80 implementation targeting the 1983 Xerox
Blue Book virtual image (Wolczko's v2 image). This is **not** Pharo,
Squeak, or any modern dialect. Semantics are intentionally preserved
as specified in Goldberg & Robson's Blue Book chapters 26–30.

Target order: Mac Catalyst first, then iOS, then Windows, then Linux.
Shared C++ core behind a pure-C API; platform UI in SwiftUI+Metal on
Apple.

Primary port source (MIT-licensed): https://github.com/dbanay/Smalltalk

**JIT policy:** JIT is allowed on every target except iOS. iOS stays
interpreter-only so we don't have to fight Apple for a JIT entitlement.

Authoritative plan: `docs/plan.md`.

## Formatting

- **Never use markdown tables** (pipes and dashes). They look terrible
  when copy-pasted into email. Use indented plain-text columns or
  bullet lists instead. Fixed-width font can be assumed.

## MANDATORY: Save Progress Every Hour

Update `docs/*.md` files and memory files at least once per hour during
long-running tasks. This is non-negotiable:

1. Every ~60 minutes, update relevant `docs/` files with current
   status, findings, and results.
2. After completing any significant milestone (test run, bug fixed),
   immediately write results to `docs/` or memory.
3. Before starting a long operation, save what you know so far.
4. Commit work-in-progress to git at least every 15 minutes (see Git
   Workflow below).
5. Use `docs/test-results.md` and `docs/changes.md` as appropriate.

## GUI Testing: MANDATORY Timeouts

Every GUI test MUST have a hard timeout that actually kills the
process. GUI testing repeatedly hangs Claude Code sessions. Never
launch the Mac Catalyst app or interact with it without a kill
mechanism.

Rules:

1. Always launch with `timeout`: `timeout 60 <command>`. Never omit.
2. Background launches need a kill timer: `sleep N && kill` safeguard.
3. AppleScript/UI automation: wrap in timeout. `timeout 10 osascript`.
4. Screenshot loops: limit iterations (max 10) and add `sleep` between.
5. **Never use `open -W`** — it waits forever.
6. Test one thing, then kill. Don't leave the app running between tests.

Pattern:

    # Launch app in background with hard kill after 90 seconds
    timeout 90 open /path/to/st80-2026.app &
    APP_PID=$!
    sleep 15  # wait for startup

    # Take screenshot with timeout
    timeout 5 screencapture -x -l <WINDOW_ID> /tmp/st80-screenshot.png

    # Kill app when done
    kill $APP_PID 2>/dev/null
    killall st80-2026 2>/dev/null

Previous sessions have hung for 28+ minutes clicking menus in a loop
with no timeout. A 60-second timeout that kills a working test is
infinitely better than no timeout that hangs forever.

## STOP: No Workarounds — Fix Root Causes

DO NOT add workarounds, hacks, or band-aids to bypass problems.

When something doesn't work:
1. STOP and understand WHY.
2. Trace the problem to its source.
3. Fix the actual bug, not the symptom.

Specific patterns that are always wrong:
- Silently swallowing errors (pushing nil and returning instead of
  letting Smalltalk handle it via DNU / error handling)
- Silently terminating processes to hide scheduler bugs
- Skipping method lookup via hardcoded class/selector checks
- Treating non-booleans as false — conditional jumps must send
  `mustBeBoolean` per the Blue Book spec
- Loop/depth detectors that silently recover — if DNU recurses
  infinitely, stop the VM; don't push nil
- C++ code doing Smalltalk's job

Before adding any workaround, ask:
1. What is the ACTUAL problem?
2. Where in the code path does it fail?
3. What would the REAL fix be?
4. Is the workaround just avoiding understanding the problem?

## Verify Visually Before Claiming Anything Works

Never claim display, menus, or interaction "works" without taking a
screenshot and examining it. Run the app, take a screenshot, and READ
it with the Read tool to confirm what's actually on screen.

### Metal Window Capture on Catalyst

`screencapture -x` CANNOT capture Metal layer content from Mac
Catalyst apps. The window appears invisible/transparent in full-screen
captures. You MUST use window-specific capture with `-l`:

    # Get window IDs for our process
    PID=$(pgrep -f "st80-2026" | head -1)
    swift -e "
    import CoreGraphics
    let windowList = CGWindowListCopyWindowInfo(.optionAll, kCGNullWindowID) as? [[String: Any]] ?? []
    for w in windowList {
        guard let ownerPID = w[kCGWindowOwnerPID as String] as? Int, ownerPID == $PID else { continue }
        let windowID = w[kCGWindowNumber as String] as? Int ?? -1
        let name = w[kCGWindowName as String] as? String ?? \"\"
        print(\"id=\(windowID) name='\(name)'\")
    }
    "

    # Capture specific window by ID
    screencapture -x -l <WINDOW_ID> /tmp/st80-screenshot.png

Checklist before claiming a GUI fix works:
1. Build and launch the Mac Catalyst app.
2. Get window ID.
3. Take a screenshot with `-l <WINDOW_ID>`.
4. Read the screenshot with the Read tool.
5. Verify EACH specific claim.

Do NOT rely on log output, test pass rates, or code analysis alone.

## Git Workflow

- Update `docs/changes.md` before committing user-visible changes.
  Add entries under the current build number at the top of the file.
- Commit at least every 15 minutes to avoid losing work. Do this
  silently without stopping to ask or show the user.
- Always run `git status` before and after commits.

## Blue Book Reference

Goldberg & Robson, "Smalltalk-80: The Language and its Implementation"
(Addison-Wesley, 1983). Chapters 26–30 are the VM specification:

    26  The Formal Specification of the Smalltalk-80 Virtual Machine
    27  Specification of the Object Memory
    28  Formal Specification of the Interpreter
    29  Formal Specification of the Primitive Methods
    30  The Initial Image

Keep citations in-code as `// BB §28.4 fig. 28.3` style references
when porting or reimplementing spec behavior. Do not reproduce the
book text.

## Port Discipline

Files containing code ported from `dbanay/Smalltalk` must carry a
header like:

    // Ported from dbanay/Smalltalk <commit-sha>:<path>
    // Copyright (c) 2020 Dan Banay, MIT License — see THIRD_PARTY_LICENSES.
    // Modifications for st80-2026 Copyright (c) 2026 Aaron Wohl, MIT.

GPL code (rochus-keller/Smalltalk) must NEVER be copied or included.
Read-only study is fine; ports are not.

## Agent Usage

For cross-file searches across ported code, primitive-table audits, or
bytecode-spec verification, delegate to the Explore agent rather than
grepping the main context — dbanay's `interpreter.cpp` and
`objmemory.cpp` are large and will fill context quickly.

## Debugging

- Debug before asking. Always run and check logs yourself first.
- Test on Mac first — it starts up much faster than the iOS simulator.
- Build with `cmake --build build` from the project root.
- Full Catalyst build once Phase 2 lands:
  `xcodebuild -project st80-2026.xcodeproj -scheme st80-2026 -configuration Debug -destination 'platform=macOS,variant=Mac Catalyst' build`
