# st80-2026 Implementation Plan

Authoritative plan for the project. Source brief: `../todo.txt`.

## Goal & non-goals

**Goal.** Run Xerox's vintage Blue Book Smalltalk-80 image (Wolczko's
`image.tar.gz`, v2) on Mac Catalyst first, then iOS, then Windows/Linux,
with a shared C++ core. Interpreter first; JIT afterwards everywhere the
host permits it.

**Non-goals.** Not Pharo. Not Squeak. Not Spur/Sista image format. Not a
modern Smalltalk. We are preserving ~1983 Blue Book semantics exactly.

## Key decisions

    Decision              Choice                                   Rationale
    --------              ------                                   ---------
    Reference impl        dbanay/Smalltalk (MIT) as port source;   MIT-clean borrow; passes Xerox trace2/trace3.
                          rochus-keller (GPL) for study only       GPL copyleft — never linked in.
    Target image          Xerox v2 (Wolczko's image.tar.gz)        All three references already target it.
    Core language         C++17                                    Matches iospharo; works on every target toolchain.
    UI stack on Apple     SwiftUI + Metal (no SDL)                 Mirrors iospharo; avoids SDL entitlement friction.
    Core-UI boundary      Pure-C header (C API)                    Keeps Swift/ObjC out of the hot path; trivial
                                                                   Win/Linux ports.
    Object memory         Port dbanay's objmemory + realwordmemory Already handles 16-bit OOPs, Object Table,
                                                                   byte-swap-on-load.
    GC                    Pure mark/sweep (no ref-counting)        1 MB heap marks in microseconds. See GC Notes.
    BitBlt                Port dbanay's bitblt.cpp                 Notoriously painful; borrow it.
    Interpreter style     Switch-dispatch behind IMethodExecutor   Swappable with a JIT backend in Phase 6.
    JIT approach          Copy-and-patch template JIT              Proven in iospharo's jit branch.
    JIT targets           Catalyst, macOS, Windows, Linux          iOS stays interpreter-only — no App Store
                                                                   entitlement fight.
    Build                 CMake + Xcode wrapper, xcframework       Copy iospharo's scripts/build-xcframework.sh.
    License               MIT                                      Honor dbanay's MIT and Xerox image terms.

## GC notes (what modern Smalltalks do, and what we do)

Modern Pharo/Cog-Spur uses generational GC: young-gen copying scavenger
(Cheney) + old-gen mark/sweep/compact; no Object Table (direct pointers);
tagged immediates for SmallInteger/Character/SmallFloat; write barrier
for the remembered set; forwarding objects to implement `become:` since
the OT is gone. VisualWorks/Cincom HPS is similar in shape. Squeak
pre-Spur kept the OT with plain mark/sweep + compact.

We are implementing **Blue Book** (1983), so we **keep the Object
Table** — it is part of the 1983 semantics, makes `become:` trivial,
and the image is ~1 MB so pauses don't matter. We use **pure
mark/sweep** (simpler than the Blue Book's ref-count + cycle-GC hybrid,
still correct, and fast enough on a 1 MB heap). Generational collection
could be layered on later by using young/old bits in OT entries, but
that is a post-JIT concern, not now.

We explicitly do **not** adopt: OT removal, tagged-immediate extensions
beyond Blue Book's odd-OOP-is-SmallInteger, forwarding objects.

## Architecture

    st80-2026/
    ├─ CMakeLists.txt                 builds libSt80Core (static)
    ├─ src/
    │  ├─ core/                       shared C++, NO platform deps, NO UI
    │  │  ├─ ObjectMemory.{cpp,hpp}   port of dbanay objmemory
    │  │  ├─ Interpreter.{cpp,hpp}    switch-dispatch; behind IMethodExecutor
    │  │  ├─ Primitives.cpp           the 128 Blue Book primitives (§28–31)
    │  │  ├─ BitBlt.{cpp,hpp}         port of dbanay
    │  │  ├─ ImageLoader.{cpp,hpp}    word-swap, OT reconstruction
    │  │  ├─ IMethodExecutor.hpp      seam for JIT; default = interpreter
    │  │  └─ hal/                     header-only hardware abstraction
    │  │     ├─ IDisplay.hpp          1-bit bitmap sink
    │  │     ├─ IInput.hpp            keyboard + 3-button mouse events
    │  │     ├─ IFileSystem.hpp       sources/changes/snapshot I/O
    │  │     └─ IClock.hpp
    │  ├─ platform/
    │  │  ├─ apple/                   UIKit + Metal bridge (Catalyst & iOS)
    │  │  ├─ win32/                   later
    │  │  └─ linux/                   later
    │  └─ include/Bridge.h            C API: st80_init, st80_load_image,
    │                                 st80_run, st80_post_mouse, st80_get_pixels, …
    ├─ app/apple/                     SwiftUI app (Catalyst + iOS targets)
    ├─ resources/                     we ship a fetcher, not the image
    ├─ scripts/build-xcframework.sh   adapted from iospharo
    ├─ docs/
    │  ├─ plan.md                     this file
    │  ├─ architecture.md             design doc — written as modules land
    │  ├─ bluebook-references.md      page refs into Goldberg & Robson
    │  └─ jit-plan.md                 Phase 6 detail
    ├─ THIRD_PARTY_LICENSES           dbanay MIT, Xerox notices
    └─ LICENSE                        MIT

## Phases

### Phase 0 — Setup
Scaffold this repo with docs, LICENSE, THIRD_PARTY_LICENSES, CLAUDE.md,
.gitignore, and the directory skeleton above. Local `git init`, then
`gh repo create avwohl/st80-2026 --private` and push.

### Phase 1 — Core VM, headless (weeks 1–3)
Port dbanay's `oops.h`, `realwordmemory.h`, `objmemory.{cpp,h}`,
`interpreter.{cpp,h}`, primitives. Stub display/input/file primitives.
Implement `ImageLoader` (big-endian byte swap; OT reconstruction).
Build `st80_trace` CLI that runs to a fixed bytecode count and diffs
against Xerox's reference trace.
**Exit:** trace2/trace3 pass byte-for-byte.

### Phase 2 — BitBlt + HAL + Metal display on Mac Catalyst (weeks 4–5)
Port `bitblt.cpp` with dbanay's test vectors. Implement IDisplay,
IInput, IFileSystem, IClock on the Apple side. Build the Catalyst
SwiftUI shell: MTKView, 1-bit → RGBA8 blit, event pump into a C event
queue (iospharo pattern). Map 1-button trackpad to ST-80's 3-button
(option = yellow, command = blue, plain = red).
**Exit:** Xerox desktop renders; Browser opens; text is legible.

### Phase 3 — iOS target (week 6)
Same xcframework, different entitlements and touch mapping (long-press
for yellow, two-finger-tap for blue). On-screen keyboard. Snapshot path
= iOS Documents dir.
**Exit:** boots on iPad; a method can be edited and saved.

### Phase 4 — Windows + Linux (weeks 7–8)
Direct2D or plain GDI on Windows. SDL2 on Linux.
**Linux landed 2026-04-17 (build 25).** `st80-linux` renders the
Xerox desktop under SDL2 on Ubuntu 25.10; `.deb` and `.rpm` packages
ship via CPack. No `#ifdef` in the platform layer — each host gets
its own files behind `IHal` and `Bridge.h`. ARM Linux next; Windows
after.
**Exit:** doIt works on both; no Apple regressions.

### Phase 5 — Polish & release gate (week 9)
Interpreter performance audit — cache OOP→address, inline-cache send
sites. Document JIT tier-up hooks.

### Phase 6 — JIT compiler (weeks 10+)
Copy-and-patch approach, mirroring iospharo's jit branch. Pre-compile
bytecode stencils with Clang, patch constants at runtime. Code zone
with LRU eviction. On macOS/Catalyst: `mmap(MAP_JIT)` +
`pthread_jit_write_protect_np` + `com.apple.security.cs.allow-jit`
entitlement. On Windows: `VirtualAlloc(PAGE_EXECUTE_READWRITE)`. On
Linux: `mmap(PROT_EXEC|PROT_WRITE)`. **iOS: interpreter only — no JIT
entitlement available for App Store distribution.**

Tier 1: per-method JIT after N calls. Tier 2: monomorphic inline-cache
specialization at send sites. Interpreter stays in the binary;
`ST80_JIT=0` env var disables JIT at runtime.

**Exit:** ≥2× speedup on Blue Book benchmarks; no correctness
regressions vs interpreter on a recorded trace.

## Risks

1. **Xerox image redistribution.** The v2 image has its own license
   terms — we ship a fetcher that downloads from Wolczko's URL rather
   than bundling.
2. **BitBlt edge cases.** Overlapping src/dst, word alignment. Port
   dbanay's unit tests alongside the code.
3. **Sources/changes persistence on iOS.** Sandboxed file system; need
   Documents-dir abstraction from day one.
4. **Tight binding between interpreter and primitives.** Keep the
   IMethodExecutor seam clean from day one so Phase 6 JIT can slot in
   without a rewrite.

## Open questions

- (resolved 2026-04-17) Phase 4 order: Linux first (x86_64 Ubuntu
  host available). Windows next.

## Prerequisites before Phase 1

- `brew install cmake` — not currently installed on this machine.
  CMake 3.20+ required for the core library build.
- Xcode + Command Line Tools — present (clang 21.0.0 confirmed).
- `gh` CLI — present, authenticated as `avwohl` via SSH.

## Conventions (from iospharo, adapted)

- Commit every ~15 min. Update `docs/changes.md` before user-visible
  commits.
- Save progress to `docs/` every ~60 min during long sessions.
- No workarounds — fix root causes.
- GUI claims require a screenshot, read with the Read tool.
- GUI tests always wrapped in `timeout N`; never `open -W`.
- No markdown tables (they render poorly when pasted to email) —
  indented plain-text columns instead.
