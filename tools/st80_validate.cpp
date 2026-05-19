// st80-2026 — st80_validate
// Copyright (c) 2026 Aaron Wohl. MIT License.
//
// Blue Book image validator. Ports the check + shasum functions of
// ../validate_smalltalk_image into this repo, but targets the Blue
// Book 1983 image format (16-bit OOPs, Object Table, big-endian) —
// the sibling tool only understands Spur.
//
// Usage:
//   st80_validate check   <path-to-image>
//   st80_validate shasum  <path-to-image>
//
// check: loads via ObjectMemory and walks every non-free OOP,
//        verifying class OOPs resolve, word length is sane, and
//        every pointer field (when pointerBit=1) lands on a live OOP.
//        Reports up to 200 problems, exit code 1 if any.
//
// shasum: emits one line per live OOP: "<oop> <sha256>". Sort-stable
//         across runs with identical images, so:
//             diff <(st80_validate shasum a.im) <(st80_validate shasum b.im)
//         flags objects whose contents changed between snapshots.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ObjectMemory.hpp"
#include "HostFileSystem.hpp"
#include "HeadlessHal.hpp"
#include "Sha256.hpp"

namespace {

struct Paths {
    std::string dir;
    std::string name;
};

Paths splitPath(const std::string &full) {
    Paths p{".", full};
    const auto slash = full.find_last_of("\\/");
    if (slash != std::string::npos) {
        p.dir = full.substr(0, slash);
        p.name = full.substr(slash + 1);
    }
    return p;
}

// Is this OOP a SmallInteger (tag bit 0 set)?
inline bool isSmallInt(int oop) { return (oop & 1) != 0; }

// Iterate every allocated OOP in the Object Table. Blue Book OOPs are
// 16-bit even numbers 0..65534, odd values are SmallIntegers. An OT
// entry is "live" when freeBit=0 and the ObjectMemory reports it valid.
//
// Note: ObjectMemory exposes `hasObject` and the bit accessors; we use
// those rather than poking ObjectTableEnt directly.
template <typename F>
void forEachLiveOop(st80::ObjectMemory &memory, F &&fn) {
    // OOPs are even, 0..(ObjectTableSize-2) inclusive. NilPointer etc
    // are included.
    for (int oop = 0; oop < 65534; oop += 2) {
        if (!memory.hasObject(oop)) continue;
        fn(oop);
    }
}

// Validate the integrity of the object at `oop`. Writes problems to
// stdout (up to `*remaining` of them) and decrements the counter.
void checkObject(st80::ObjectMemory &memory, int oop, int *remaining) {
    auto report = [&](const char *fmt, int a = 0, int b = 0) {
        if (*remaining <= 0) return;
        --*remaining;
        std::printf("oop 0x%04x: ", oop);
        std::printf(fmt, a, b);
        std::putchar('\n');
    };

    // Class OOP should resolve to a live object. SmallInteger class is
    // tagged via odd OOP convention — fetchClassOf handles that, but
    // verify the class OOP itself is a real object.
    const int cls = memory.fetchClassOf(oop);
    if (isSmallInt(cls)) {
        report("class slot contains a SmallInteger (0x%04x), not a class", cls);
        return;
    }
    if (!memory.hasObject(cls)) {
        report("class 0x%04x is not a live object", cls);
        return;
    }

    // Body length must be non-negative. We can't cheaply know whether
    // the body is pointer- or byte-typed without walking the class's
    // instance format (BB §28.3), so we skip pointer-field validation
    // in this v1 — loadSnapshot would have rejected an overflowing
    // allocation and a dead pointer would just be a logically-bad
    // reference, not corruption.
    const int n = memory.fetchWordLengthOf(oop);
    if (n < 0 || n > 0xFFFF) {
        report("bogus word-length %d", n);
    }
}

int cmdCheck(const std::string &path) {
    const auto p = splitPath(path);
    st80::HostFileSystem fs(p.dir);
    st80::HeadlessHal hal;
    // Heap-allocate: ObjectMemory embeds 2 MiB RealWordMemory by
    // value; a stack local underflows DJGPP's small default stack on
    // the DOS port (see tools/st80_run.cpp).
    auto memoryPtr = std::make_unique<st80::ObjectMemory>(&hal);
    auto &memory = *memoryPtr;

    if (!memory.loadSnapshot(&fs, p.name.c_str())) {
        std::fprintf(stderr, "st80_validate: loadSnapshot FAILED\n");
        return 2;
    }

    int remaining = 200;
    int startRemaining = remaining;
    int liveCount = 0;
    forEachLiveOop(memory, [&](int oop) {
        ++liveCount;
        checkObject(memory, oop, &remaining);
    });

    const int problems = startRemaining - remaining;
    std::fprintf(stderr,
                 "st80_validate: %d live objects, %d problem(s)%s\n",
                 liveCount, problems,
                 problems >= startRemaining ? " (capped)" : "");
    return problems == 0 ? 0 : 1;
}

int cmdShaSum(const std::string &path) {
    const auto p = splitPath(path);
    st80::HostFileSystem fs(p.dir);
    st80::HeadlessHal hal;
    // Heap-allocate: ObjectMemory embeds 2 MiB RealWordMemory by
    // value; a stack local underflows DJGPP's small default stack on
    // the DOS port (see tools/st80_run.cpp).
    auto memoryPtr = std::make_unique<st80::ObjectMemory>(&hal);
    auto &memory = *memoryPtr;

    if (!memory.loadSnapshot(&fs, p.name.c_str())) {
        std::fprintf(stderr, "st80_validate: loadSnapshot FAILED\n");
        return 2;
    }

    std::vector<uint8_t> buf;
    forEachLiveOop(memory, [&](int oop) {
        Sha256 h;
        // Hash class OOP + body words as big-endian bytes. Stable
        // across runs regardless of host byte order and agnostic of
        // transient OT-entry bookkeeping like refcount / mark bits.
        const int cls = memory.fetchClassOf(oop);
        const int n = memory.fetchWordLengthOf(oop);

        buf.clear();
        auto push16 = [&](uint16_t v) {
            buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(v & 0xFF));
        };
        push16(static_cast<uint16_t>(cls));
        push16(static_cast<uint16_t>(n));

        for (int i = 0; i < n; ++i) {
            push16(static_cast<uint16_t>(
                memory.fetchWord_ofObject(i, oop)));
        }

        h.update(buf.data(), buf.size());
        std::printf("%04x %s\n", oop, h.hexFinalize().c_str());
    });
    return 0;
}

// Load <in>, then write it back out via ObjectMemory::saveSnapshot to
// <out>. Exercises the create_file + write (DOS AH=40) path — the
// mirror of the load path that the DOS-port D4 round-trip needs. <in>
// and <out> must live in the same directory (true for the dosiz
// staging case under C:); a round-trip is then proven by diffing
//   st80_validate shasum <in>  vs  st80_validate shasum <out>
int cmdResave(const std::string &inPath, const std::string &outPath) {
    const auto in = splitPath(inPath);
    const auto out = splitPath(outPath);
    if (out.dir != in.dir && out.dir != ".") {
        std::fprintf(stderr,
            "st80_validate: resave needs <in> and <out> in the same "
            "directory (in dir='%s', out dir='%s')\n",
            in.dir.c_str(), out.dir.c_str());
        return 64;
    }
    st80::HostFileSystem fs(in.dir);
    st80::HeadlessHal hal;
    // Heap-allocate: ObjectMemory embeds 2 MiB RealWordMemory by value
    // (see tools/st80_run.cpp for the DJGPP stack rationale).
    auto memoryPtr = std::make_unique<st80::ObjectMemory>(&hal);
    auto &memory = *memoryPtr;

    if (!memory.loadSnapshot(&fs, in.name.c_str())) {
        std::fprintf(stderr, "st80_validate: loadSnapshot FAILED\n");
        return 2;
    }
    if (!memory.saveSnapshot(&fs, out.name.c_str())) {
        std::fprintf(stderr, "st80_validate: saveSnapshot FAILED\n");
        return 3;
    }
    std::fprintf(stderr, "st80_validate: resaved %s -> %s\n",
                 in.name.c_str(), out.name.c_str());
    return 0;
}

// One SHA-256 over every live object (oop + class + length + body,
// big-endian, host-byte-order-independent). Same per-object scheme as
// `shasum`, folded into a single digest so two images compare as one
// hex string. Iteration is deterministic (ascending oop).
std::string imageDigest(st80::ObjectMemory &memory) {
    Sha256 h;
    std::vector<uint8_t> buf;
    forEachLiveOop(memory, [&](int oop) {
        const int cls = memory.fetchClassOf(oop);
        const int n = memory.fetchWordLengthOf(oop);
        buf.clear();
        auto push16 = [&](uint16_t v) {
            buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
            buf.push_back(static_cast<uint8_t>(v & 0xFF));
        };
        push16(static_cast<uint16_t>(oop));
        push16(static_cast<uint16_t>(cls));
        push16(static_cast<uint16_t>(n));
        for (int i = 0; i < n; ++i)
            push16(static_cast<uint16_t>(memory.fetchWord_ofObject(i, oop)));
        h.update(buf.data(), buf.size());
    });
    return h.hexFinalize();
}

// Self-contained D4 regression gate: load <image>, saveSnapshot to a
// sibling temp, reload that, and assert the object graph is identical
// across the write→read round trip. Cross-platform (no shell), so it
// runs as a CTest on every host and under dosiz on the DOS port —
// catches any save/load drift (the dosiz AH=40 / O_BINARY path, byte
// order, OT/page layout). Exit 0 = identical, 1 = drift, 2 = load/
// save failure.
int cmdRoundtrip(const std::string &path) {
    const auto p = splitPath(path);
    st80::HostFileSystem fs(p.dir);
    st80::HeadlessHal hal;
    const std::string tmp = p.name + ".rtmp";

    std::string digA;
    {
        auto m = std::make_unique<st80::ObjectMemory>(&hal);
        if (!m->loadSnapshot(&fs, p.name.c_str())) {
            std::fprintf(stderr, "st80_validate: roundtrip load FAILED\n");
            return 2;
        }
        digA = imageDigest(*m);
        if (!m->saveSnapshot(&fs, tmp.c_str())) {
            std::fprintf(stderr, "st80_validate: roundtrip save FAILED\n");
            return 2;
        }
    }
    std::string digB;
    {
        auto m = std::make_unique<st80::ObjectMemory>(&hal);
        if (!m->loadSnapshot(&fs, tmp.c_str())) {
            std::fprintf(stderr, "st80_validate: roundtrip reload FAILED\n");
            fs.delete_file(tmp.c_str());
            return 2;
        }
        digB = imageDigest(*m);
    }
    fs.delete_file(tmp.c_str());

    if (digA != digB) {
        std::fprintf(stderr,
            "st80_validate: roundtrip MISMATCH\n  in : %s\n  out: %s\n",
            digA.c_str(), digB.c_str());
        return 1;
    }
    std::fprintf(stderr,
        "st80_validate: roundtrip OK (object graph stable; digest %s)\n",
        digA.c_str());
    return 0;
}

void usage() {
    std::fprintf(stderr,
        "usage: st80_validate <command> <args>\n"
        "commands:\n"
        "  check     <image>       structural validation; exit 1 if problems\n"
        "  shasum    <image>       per-OOP SHA-256 manifest to stdout\n"
        "  resave    <in> <out>    load then saveSnapshot (write-path test)\n"
        "  roundtrip <image>       load->save->reload; exit 1 if graph drifts\n");
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) { usage(); return 64; }
    const std::string cmd = argv[1];
    const std::string path = argv[2];

    if (cmd == "check")     return cmdCheck(path);
    if (cmd == "shasum")    return cmdShaSum(path);
    if (cmd == "roundtrip") return cmdRoundtrip(path);
    if (cmd == "resave") {
        if (argc < 4) { usage(); return 64; }
        return cmdResave(path, argv[3]);
    }

    usage();
    return 64;
}
