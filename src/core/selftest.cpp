#include "selftest.h"

#include <cstdio>
#include <cstring>

#include "crc32.h"
#include "pe.h"
#include "stream_cipher.h"

namespace rawr {
namespace {

struct Checker {
    SelfTestResult result;

    void check(bool ok, const std::string& name, const std::string& detail = std::string()) {
        result.checks++;
        if (!ok) result.failures++;
        std::string line = ok ? "  ok   " : "  FAIL ";
        line += name;
        if (!detail.empty()) line += "  (" + detail + ")";
        result.lines.push_back(line);
    }

    void section(const std::string& name) { result.lines.push_back(name); }
};

std::string toHex(const Bytes& data) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t b : data) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0xF]);
    }
    return out;
}

// The Reflexive keystream for a known seed. These vectors were produced by
// the wrapper's own maths and are the anchor for the whole cipher.
struct KeystreamVector {
    uint32_t seed;
    const char* expected;
};

const KeystreamVector kKeystreamVectors[] = {
    {0x00000000u, "098305f1e7ba418491c936fe81fc68a0539ce025fd1240e7"},
    {0x00000001u, "eaa336ee3aa12f304035e3ae2eb386a271e67c8d0a7f66cb"},
    {0xC5353E8Bu, "90aaf09f79277b780e962c93e1a96ef6dc1db23aa5617b94"},
    {0xFFFFFFFFu, "19ab5d9ead94b7277f68792e6ea642efb65ab1dee231f2f6"},
    {0x12345678u, "59e08ed10e9bb4e41b1567aca25dab54fc9b9ad9dee835c8"},
};

void testCrc32(Checker& c) {
    c.section("crc32");
    c.check(crc32_of("123456789", 9) == 0xCBF43926u, "crc32(\"123456789\") == 0xcbf43926");
    c.check(crc32_of("", 0) == 0x00000000u, "crc32 of an empty buffer is zero");
    c.check(crc32_of("a", 1) == 0xE8B7BE43u, "crc32(\"a\") == 0xe8b7be43");
    // Incremental updates must match a single pass.
    uint32_t crc = crc32_update(0, "12345", 5);
    crc = crc32_update(crc, "6789", 4);
    c.check(crc == 0xCBF43926u, "incremental crc32 matches a single pass");
}

void testStreamCipher(Checker& c) {
    c.section("reflexive stream cipher");
    for (const KeystreamVector& v : kKeystreamVectors) {
        Bytes zeros(24, 0);
        stream_decrypt_inplace(zeros.data(), zeros.size(), v.seed);
        std::string got = toHex(zeros);
        char label[32];
        std::snprintf(label, sizeof(label), "keystream for seed 0x%08X", v.seed);
        c.check(got == v.expected, label, got);
    }
    // The cipher subtracts the keystream, so the copy and the in place
    // variants must agree and the result must differ from the input.
    Bytes plain = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48};
    Bytes copy = stream_decrypt_copy(plain.data(), plain.size(), 0xDEADBEEFu);
    Bytes inplace = plain;
    stream_decrypt_inplace(inplace.data(), inplace.size(), 0xDEADBEEFu);
    c.check(copy == inplace, "the copy and in place variants agree");
    c.check(copy != plain, "decryption changes the data");
    // Seed zero has a known first key byte, so the first output byte is
    // fully determined.
    Bytes one = {0x41};
    stream_decrypt_inplace(one.data(), 1, 0);
    c.check(one[0] == 0x4A, "seed 0 maps 0x41 to 0x4a");
}

void testResources(Checker& c) {
    c.section("resource tree");
    // Build a small resource tree with a named group, write it into a
    // synthetic PE and read it back. This is the round trip that catches
    // offset mistakes in the writer.
    std::vector<ResourceLeaf> leaves;
    ResourceLeaf icon;
    icon.type = kRtIcon;
    icon.nameId = 1;
    icon.language = 0x0409;
    icon.data = Bytes(64, 0xAB);
    leaves.push_back(icon);

    ResourceLeaf group;
    group.type = kRtGroupIcon;
    group.nameIsNamed = true;
    group.nameString = "MAINICON";
    group.language = 0x0409;
    group.data = Bytes(6 + 14, 0);
    group.data[4] = 1;  // one icon in the group
    group.data[5] = 0;
    group.data[6 + 12] = 1;  // referring to icon id 1
    leaves.push_back(group);

    // A minimal PE32 to carry it.
    Bytes pe(0x400, 0);
    auto put8 = [&](size_t off, uint8_t v) { pe[off] = v; };
    auto put16 = [&](size_t off, uint16_t v) {
        pe[off] = static_cast<uint8_t>(v & 0xFF);
        pe[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    };
    auto put32 = [&](size_t off, uint32_t v) {
        pe[off] = static_cast<uint8_t>(v & 0xFF);
        pe[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        pe[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        pe[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };
    put8(0, 'M');
    put8(1, 'Z');
    put32(0x3C, 0x80);
    put8(0x80, 'P');
    put8(0x81, 'E');
    put16(0x84, 0x014C);
    put16(0x86, 1);
    put16(0x94, 0xE0);
    put16(0x98, 0x010B);
    put32(0x98 + 16, 0x10);
    put32(0x98 + 28, 0x00400000);
    put32(0x98 + 32, 0x1000);
    put32(0x98 + 36, 0x200);
    put32(0x98 + 56, 0x2000);
    put32(0x98 + 60, 0x200);
    put16(0x98 + 68, 2);
    std::memcpy(pe.data() + 0x178, ".text", 5);
    put32(0x178 + 8, 0x10);
    put32(0x178 + 12, 0x10);
    put32(0x178 + 16, 0x10);
    put32(0x178 + 20, 0x200);

    std::string err;
    bool wrote = pe_write_resources(pe, leaves, err);
    c.check(wrote, "the resource section was written into a PE", err);

    PeImage image;
    if (!pe_parse(pe, image, err)) {
        c.check(false, "the PE still parses after the resource write", err);
        return;
    }
    std::vector<ResourceLeaf> back;
    bool read = pe_read_resources(image, back, err);
    c.check(read, "the resource tree reads back", err);
    if (!read) return;

    c.check(back.size() == leaves.size(), "the same number of leaves came back",
            std::to_string(back.size()) + " of " + std::to_string(leaves.size()));

    bool foundIcon = false, foundGroup = false;
    for (const ResourceLeaf& leaf : back) {
        if (leaf.type == kRtIcon && leaf.nameId == 1) {
            foundIcon = leaf.data.size() == 64 && leaf.data[0] == 0xAB;
        }
        if (leaf.type == kRtGroupIcon) {
            foundGroup = leaf.nameIsNamed && leaf.nameString == "MAINICON" &&
                         leaf.data.size() == 20;
        }
    }
    c.check(foundIcon, "the icon payload survived the round trip");
    c.check(foundGroup, "the named group survived the round trip");
}

void testPe(Checker& c) {
    c.section("pe reader");
    // A hand built PE32 with one section, used to exercise the reader
    // without depending on the host platform.
    Bytes pe(0x400, 0);
    auto put8 = [&](size_t off, uint8_t v) { pe[off] = v; };
    auto put16 = [&](size_t off, uint16_t v) {
        pe[off] = static_cast<uint8_t>(v & 0xFF);
        pe[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    };
    auto put32 = [&](size_t off, uint32_t v) {
        pe[off] = static_cast<uint8_t>(v & 0xFF);
        pe[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        pe[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        pe[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    };

    put8(0, 'M');
    put8(1, 'Z');
    put32(0x3C, 0x80);                      // e_lfanew
    put8(0x80, 'P');
    put8(0x81, 'E');
    put16(0x84, 0x014C);                    // machine i386
    put16(0x86, 1);                         // one section
    put16(0x94, 0xE0);                      // size of optional header
    put16(0x98, 0x010B);                    // magic PE32
    put32(0x98 + 16, 0x10);                 // entry point RVA
    put32(0x98 + 28, 0x00400000);           // image base
    put32(0x98 + 32, 0x1000);               // section alignment
    put32(0x98 + 36, 0x200);                // file alignment
    put32(0x98 + 56, 0x2000);               // size of image
    put32(0x98 + 60, 0x200);                // size of headers
    put16(0x98 + 68, 2);                    // subsystem: windows gui
    // section table at 0x80 + 24 + 0xE0 = 0x178
    std::memcpy(pe.data() + 0x178, ".text", 5);
    put32(0x178 + 8, 0x10);                 // virtual size
    put32(0x178 + 12, 0x10);                // virtual address
    put32(0x178 + 16, 0x10);                // raw size
    put32(0x178 + 20, 0x200);               // raw offset

    PeImage image;
    std::string err;
    bool ok = pe_parse(pe, image, err);
    c.check(ok, "a hand built PE32 parses", err);
    if (ok) {
        c.check(image.machine == 0x14C, "the machine type is i386");
        c.check(image.numberOfSections == 1, "the section count is right");
        c.check(image.sections.size() == 1 && image.sections[0].name == ".text",
                "the section name is read");
        c.check(pe_rva_to_offset(image, 0x10) == 0x200, "the entry point maps to its raw offset");
        c.check(pe_looks_like_program(pe, err), "the hand built PE passes the sanity check", err);
    }

    // A buffer that is not a PE must be rejected with a clear reason.
    Bytes junk(64, 0x41);
    PeImage bad;
    std::string badErr;
    c.check(!pe_parse(junk, bad, badErr) && !badErr.empty(),
            "a non PE buffer is rejected with a reason");
}

} // namespace

SelfTestResult selftest_run() {
    Checker c;
    testCrc32(c);
    testStreamCipher(c);
    testResources(c);
    testPe(c);
    c.result.passed = c.result.failures == 0;
    return c.result;
}

} // namespace rawr
