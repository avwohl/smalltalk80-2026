# JIT Plan (Phase 6)

Deferred until the interpreter passes Phase 5. This document exists
to (a) keep the interpreter architecture honest about what the JIT
will need, and (b) avoid re-deriving the iOS decisions when the time
comes.

## Scope

JIT targets: Mac Catalyst, macOS, Windows, Linux.
**Not** iOS — no App Store JIT entitlement. iOS stays interpreter-only
in every configuration.

## Approach

Copy-and-patch template JIT, modeled on iospharo's `jit` branch:

1. Pre-compile bytecode stencils with Clang (one stencil per
   bytecode/short-form combination).
2. Extract their machine code plus relocation "holes" into a generated
   header.
3. At JIT time: concatenate stencils and patch the holes with runtime
   constants (literal OOPs, method addresses, inline-cache slots).

Two tiers:
- **T1** — method-level JIT after N interpreter calls (N ≈ 32). Default
  specialization; no send-site inline caches yet.
- **T2** — re-JIT at send sites once inline-cache data shows
  monomorphic or polymorphic-with-small-degree behavior.

## Code zone

Bump-allocated executable region with LRU eviction when full. Size
default 16 MB; tunable via `ST80_JIT_CACHE_MB`. On eviction the VM
falls back to the interpreter for evicted methods until they heat up
again.

## Platform W^X handling

    macOS / Catalyst (Apple Silicon):
        mmap(MAP_JIT | PROT_READ | PROT_WRITE | PROT_EXEC)
        pthread_jit_write_protect_np(0)  // unlock for write
        ... write code ...
        pthread_jit_write_protect_np(1)  // lock for execute
        sys_icache_invalidate(code, size)

    macOS / Catalyst (Intel):
        mprotect(rw) → write → mprotect(rx)

    Windows:
        VirtualAlloc(PAGE_READWRITE) → write →
        VirtualProtect(PAGE_EXECUTE_READ) → FlushInstructionCache

    Linux:
        mmap(PROT_READ | PROT_WRITE | PROT_EXEC)
        (or mprotect-shuffle on hardened kernels)

## Apple entitlements

    com.apple.security.cs.allow-jit
    com.apple.security.cs.allow-unsigned-executable-memory  (may be needed)
    com.apple.security.cs.jit-write-to-executable-memory    (optional newer API)

These are for macOS and Catalyst distribution. iOS does not get
`allow-jit` on App Store builds, which is why iOS stays interpreter.

## Interpreter seam (Phase 1 must not forget)

The interpreter dispatches methods through `IMethodExecutor`. Two
implementations:

    InterpreterExecutor         default; the switch-dispatch loop
    JitExecutor                 Phase 6; falls back to InterpreterExecutor
                                when the method has not yet been compiled

The JIT tier-up trigger (a per-method call count) is a field on the
interpreter's internal compiled-method representation, not on the
image-level CompiledMethod object — we must not mutate the image for
a VM-internal counter.

## Fallback

`ST80_JIT=0` env var forces interpreter-only at runtime on any target.
The interpreter is always in the binary. There is no "JIT-only" build.

## Success criterion

≥2× speedup on Blue Book's tinyBenchmarks, zero correctness
regressions against a recorded interpreter trace.
