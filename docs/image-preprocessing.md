# Image preprocessing: BE → LE

The Xerox Smalltalk-80 v2 virtual image (Wolczko's `VirtualImage`) is
stored in big-endian "interchange" format — the Xerox Alto and Dorado
were big-endian machines. Modern macOS / Linux / Windows hosts are
little-endian, so raw loading fails.

dbanay's approach (which we've inherited) is **one-time offline
preprocessing**: run `imageswapper.c` once to produce a `VirtualImageLSB`
file that the loader reads directly.

## What the swap does

    For every 32-bit header field:      full 4-byte swap (swap32).
    For every 16-bit word in objects
      whose class is pointer-containing
      or WordArray or DisplayBitmap:    2-byte swap (swap16) per word.
    For CompiledMethod objects:         swap the size, class, method
                                        header, and each literal — but
                                        *not* the bytecode bytes.
    For byte-type objects (Strings,
      Symbols, ByteArrays):             swap only size and class;
                                        byte-body stays as-is.
    For Float objects:                  4-byte swap (big-endian IEEE
                                        → little-endian IEEE).
    For the Object Table:               swap16 every word.

The asymmetry matters: byte-indexed objects rely on `byteNumber=0`
still returning the first file byte after load.

Reference: `reference/dbanay-smalltalk/misc/imageswapper.c`.

## Current status — Path 2 landed (build 7)

The loader is endian-aware. `ObjectMemory::loadObjectTable` inspects
the header's `objectTableLength`; if a host-native int32 read produces
an implausible value (≤0 or > fileSize/2), it byte-swaps the header
and sets the private `swapOnLoad` flag. `loadObjects` swaps each
incoming size/class word and post-processes each object's body via
`swapObjectBodyBytes`, dispatching on class:

    CompiledMethod     swap header word + each literal; leave
                       bytecode bytes raw
    Float              4-byte reversal across the two body words
    pointer objects,
    DisplayBitmap,
    WordArray          swap every body word
    byte-type objects  no body swap (bytes are endian-agnostic)

Verified: `st80_probe` against both the raw Xerox `VirtualImage`
(big-endian) and dbanay's pre-swapped `VirtualImageLSB`
(little-endian) produces identical stats — `oopsLeft = 14375`,
`coreLeft = 723822 words`.

No preprocessing step required. Users drop the Xerox image in and
the loader handles it.

## Saving

`saveObjects` still writes host-native (matches dbanay's behavior).
Round-trip on the same host works because auto-detect reads
host-native as plausible and skips the swap. Cross-host portability
on saved images also works because the swap fallback kicks in. We
did not change `saveObjects` for this commit; if we ever want
saved images canonical BE for archival, that's a separate change.

## File sizes (reference)

    VirtualImage        596,128 bytes (big-endian, from Wolczko)
    VirtualImageLSB     596,128 bytes (little-endian, from dbanay)
    Smalltalk-80.sources  1,411,072 bytes (text, ASCII — no swap)
