#include "pe.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace rawr {
namespace {

constexpr uint32_t kResourceDirIndex = 2;
constexpr uint32_t kSectionCharInitializedData = 0x00000040u;
constexpr uint32_t kSectionCharRead = 0x40000000u;

inline uint16_t rd16(const Bytes& d, size_t o) {
    return static_cast<uint16_t>(d[o] | (d[o + 1] << 8));
}
inline uint32_t rd32(const Bytes& d, size_t o) {
    return static_cast<uint32_t>(d[o]) | (static_cast<uint32_t>(d[o + 1]) << 8) |
           (static_cast<uint32_t>(d[o + 2]) << 16) | (static_cast<uint32_t>(d[o + 3]) << 24);
}
inline void wr16(Bytes& d, size_t o, uint16_t v) {
    d[o] = static_cast<uint8_t>(v & 0xFF);
    d[o + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
inline void wr32(Bytes& d, size_t o, uint32_t v) {
    d[o] = static_cast<uint8_t>(v & 0xFF);
    d[o + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    d[o + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    d[o + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

inline uint32_t align_up(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) / alignment * alignment;
}

// The data directories start at a different offset in PE32 and PE32+.
inline uint32_t data_dir_offset(const PeImage& pe) {
    return pe.optionalOffset + (pe.is64 ? 112u : 96u);
}

std::string utf16_to_utf8(const Bytes& d, size_t offset, size_t charCount) {
    std::string out;
    for (size_t i = 0; i < charCount; ++i) {
        uint16_t c = rd16(d, offset + i * 2);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// Resource tree walking -----------------------------------------------------

struct RawDirEntry {
    uint32_t nameField = 0;
    uint32_t offsetField = 0;
    std::string nameString;
    bool isNamed = false;
    uint16_t id = 0;
};

bool read_dir(const Bytes& d, size_t base, uint32_t dirOffset, std::vector<RawDirEntry>& out,
              std::string& err) {
    // Directory offsets inside a resource section are relative to the start
    // of that section, not to the start of the file.
    size_t dir = base + dirOffset;
    if (dir + 16 > d.size()) {
        err = "resource directory out of bounds";
        return false;
    }
    uint16_t named = rd16(d, dir + 12);
    uint16_t ids = rd16(d, dir + 14);
    size_t count = static_cast<size_t>(named) + ids;
    size_t entryBase = dir + 16;
    if (entryBase + count * 8 > d.size()) {
        err = "resource directory entries out of bounds";
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        size_t e = entryBase + i * 8;
        RawDirEntry entry;
        entry.nameField = rd32(d, e);
        entry.offsetField = rd32(d, e + 4);
        if (entry.nameField & 0x80000000u) {
            uint32_t noff = entry.nameField & 0x7FFFFFFFu;
            size_t nameOff = base + noff;
            if (nameOff + 2 > d.size()) {
                err = "resource name out of bounds";
                return false;
            }
            uint16_t len = rd16(d, nameOff);
            if (nameOff + 2 + static_cast<size_t>(len) * 2 > d.size()) {
                err = "resource name out of bounds";
                return false;
            }
            entry.isNamed = true;
            entry.nameString = utf16_to_utf8(d, nameOff + 2, len);
        } else {
            entry.id = static_cast<uint16_t>(entry.nameField);
        }
        out.push_back(entry);
    }
    return true;
}

} // namespace

bool pe_parse(const Bytes& data, PeImage& out, std::string& err) {
    if (data.size() < 0x40) {
        err = "file is too small to be a PE image";
        return false;
    }
    if (data[0] != 'M' || data[1] != 'Z') {
        err = "missing MZ signature";
        return false;
    }
    uint32_t peOffset = rd32(data, 0x3C);
    if (peOffset + 24 > data.size()) {
        err = "PE header offset out of bounds";
        return false;
    }
    if (data[peOffset] != 'P' || data[peOffset + 1] != 'E' || data[peOffset + 2] != 0 ||
        data[peOffset + 3] != 0) {
        err = "missing PE signature";
        return false;
    }

    out = PeImage();
    out.data = data;
    out.peOffset = peOffset;
    out.machine = rd16(data, peOffset + 4);
    out.numberOfSections = rd16(data, peOffset + 6);
    out.sizeOfOptionalHeader = rd16(data, peOffset + 20);
    out.optionalOffset = peOffset + 24;
    if (out.optionalOffset + out.sizeOfOptionalHeader > data.size()) {
        err = "optional header out of bounds";
        return false;
    }

    uint16_t magic = rd16(data, out.optionalOffset);
    if (magic == 0x20B) {
        out.is64 = true;
    } else if (magic == 0x10B) {
        out.is64 = false;
    } else {
        err = "unknown optional header magic";
        return false;
    }

    out.addressOfEntryPoint = rd32(data, out.optionalOffset + 16);
    out.imageBase = out.is64 ? rd32(data, out.optionalOffset + 24)
                             : rd32(data, out.optionalOffset + 28);
    out.sectionAlignment = rd32(data, out.optionalOffset + 32);
    out.fileAlignment = rd32(data, out.optionalOffset + 36);
    out.sizeOfImage = rd32(data, out.optionalOffset + 56);
    out.sizeOfHeaders = rd32(data, out.optionalOffset + 60);
    out.subsystem = rd16(data, out.optionalOffset + 68);

    uint32_t dirOffset = out.optionalOffset + (out.is64 ? 112 : 96);
    for (int i = 0; i < 16; ++i) {
        if (dirOffset + static_cast<uint32_t>(i) * 8 + 8 > data.size()) break;
        out.dataDirRva[i] = rd32(data, dirOffset + i * 8);
        out.dataDirSize[i] = rd32(data, dirOffset + i * 8 + 4);
    }

    out.sectionTableOffset = out.optionalOffset + out.sizeOfOptionalHeader;
    for (uint16_t i = 0; i < out.numberOfSections; ++i) {
        size_t o = out.sectionTableOffset + static_cast<size_t>(i) * 40;
        if (o + 40 > data.size()) {
            err = "section table out of bounds";
            return false;
        }
        PeSection s;
        char name[9] = {0};
        std::memcpy(name, data.data() + o, 8);
        s.name = name;
        s.virtualSize = rd32(data, o + 8);
        s.virtualAddress = rd32(data, o + 12);
        s.sizeOfRawData = rd32(data, o + 16);
        s.pointerToRawData = rd32(data, o + 20);
        s.characteristics = rd32(data, o + 36);
        out.sections.push_back(s);
    }
    return true;
}

uint32_t pe_rva_to_offset(const PeImage& pe, uint32_t rva) {
    for (const PeSection& s : pe.sections) {
        uint32_t span = std::max(s.virtualSize, s.sizeOfRawData);
        if (rva >= s.virtualAddress && rva < s.virtualAddress + span)
            return s.pointerToRawData + (rva - s.virtualAddress);
    }
    // Inside the headers?
    if (rva < pe.sizeOfHeaders) return rva;
    return 0;
}

bool pe_has_resources(const PeImage& pe) {
    return pe.dataDirRva[kResourceDirIndex] != 0 && pe.dataDirSize[kResourceDirIndex] != 0;
}

bool pe_read_resources(const PeImage& pe, std::vector<ResourceLeaf>& out, std::string& err) {
    out.clear();
    if (!pe_has_resources(pe)) return true;

    uint32_t resRva = pe.dataDirRva[kResourceDirIndex];
    uint32_t resOff = pe_rva_to_offset(pe, resRva);
    if (resOff == 0) {
        err = "resource directory is not mapped to any section";
        return false;
    }
    // The whole resource section is addressed relative to its own base.
    const Bytes& d = pe.data;
    size_t resBase = resOff;

    std::vector<RawDirEntry> typeEntries;
    if (!read_dir(d, resBase, 0, typeEntries, err)) return false;

    for (const RawDirEntry& te : typeEntries) {
        if (!(te.offsetField & 0x80000000u)) {
            err = "unexpected resource layout (type without subdirectory)";
            return false;
        }
        uint32_t typeDir = te.offsetField & 0x7FFFFFFFu;
        std::vector<RawDirEntry> nameEntries;
        if (!read_dir(d, resBase, typeDir, nameEntries, err)) return false;

        for (const RawDirEntry& ne : nameEntries) {
            if (!(ne.offsetField & 0x80000000u)) {
                err = "unexpected resource layout (name without subdirectory)";
                return false;
            }
            uint32_t nameDir = ne.offsetField & 0x7FFFFFFFu;
            std::vector<RawDirEntry> langEntries;
            if (!read_dir(d, resBase, nameDir, langEntries, err)) return false;

            for (const RawDirEntry& le : langEntries) {
                if (le.offsetField & 0x80000000u) {
                    err = "unexpected resource layout (language with subdirectory)";
                    return false;
                }
                uint32_t dataEntryOff = le.offsetField;
                if (dataEntryOff + 16 > d.size()) {
                    err = "resource data entry out of bounds";
                    return false;
                }
                uint32_t payloadRva = rd32(d, resBase + dataEntryOff);
                uint32_t payloadSize = rd32(d, resBase + dataEntryOff + 4);
                uint32_t payloadOff = pe_rva_to_offset(pe, payloadRva);
                if (payloadOff == 0 || payloadOff + payloadSize > d.size()) {
                    err = "resource payload out of bounds";
                    return false;
                }
                ResourceLeaf leaf;
                leaf.typeIsNamed = te.isNamed;
                leaf.type = te.id;
                leaf.typeName = te.nameString;
                leaf.nameIsNamed = ne.isNamed;
                leaf.nameId = ne.id;
                leaf.nameString = ne.nameString;
                leaf.language = le.id;
                leaf.data.assign(d.begin() + payloadOff, d.begin() + payloadOff + payloadSize);
                out.push_back(std::move(leaf));
            }
        }
    }
    return true;
}

Bytes build_resource_section(const std::vector<ResourceLeaf>& leaves, uint32_t base_rva) {
    // Group by type -> name -> language, keeping the tree ordering rules
    // (named entries first, then numeric ids, each group sorted).
    struct NameKey {
        bool named = false;
        uint16_t id = 0;
        std::string str;
        bool operator<(const NameKey& o) const {
            // Named entries have to come before the numbered ones, and both
            // groups have to be sorted.
            if (named != o.named) return named;   // named (true) sorts first
            if (named) return str < o.str;
            return id < o.id;
        }
    };
    struct TypeKey {
        bool named = false;
        uint16_t id = 0;
        std::string str;
        bool operator<(const TypeKey& o) const {
            if (named != o.named) return named;
            if (named) return str < o.str;
            return id < o.id;
        }
    };
    struct LeafRef {
        const ResourceLeaf* leaf;
        uint16_t language;
    };
    struct NameGroup {
        NameKey key;
        std::vector<LeafRef> leaves;
    };
    struct TypeGroup {
        TypeKey key;
        std::vector<NameGroup> names;
    };

    std::map<TypeKey, std::map<NameKey, std::vector<LeafRef>>> tree;
    for (const ResourceLeaf& leaf : leaves) {
        TypeKey tk{leaf.typeIsNamed, leaf.type, leaf.typeName};
        NameKey nk{leaf.nameIsNamed, leaf.nameId, leaf.nameString};
        tree[tk][nk].push_back(LeafRef{&leaf, leaf.language});
    }

    std::vector<TypeGroup> types;
    for (auto& t : tree) {
        TypeGroup tg;
        tg.key = t.first;
        for (auto& n : t.second) {
            NameGroup ng;
            ng.key = n.first;
            ng.leaves = n.second;
            std::sort(ng.leaves.begin(), ng.leaves.end(),
                      [](const LeafRef& a, const LeafRef& b) { return a.language < b.language; });
            tg.names.push_back(std::move(ng));
        }
        types.push_back(std::move(tg));
    }

    // Pass 1: sizes.
    size_t totalLeaves = 0;
    size_t nameStringBytes = 0;
    size_t blobBytes = 0;
    for (const TypeGroup& tg : types) {
        for (const NameGroup& ng : tg.names) {
            totalLeaves += ng.leaves.size();
            if (ng.key.named) nameStringBytes += 2 + ng.key.str.size() * 2;
            for (const LeafRef& lr : ng.leaves) blobBytes += align_up(lr.leaf->data.size(), 4);
        }
    }
    for (const TypeGroup& tg : types) {
        if (tg.key.named) nameStringBytes += 2 + tg.key.str.size() * 2;
    }

    const size_t rootSize = 16 + 8 * types.size();
    size_t cursor = align_up(rootSize, 4);

    std::vector<size_t> typeDirOffset(types.size());
    for (size_t i = 0; i < types.size(); ++i) {
        typeDirOffset[i] = cursor;
        cursor = align_up(cursor + 16 + 8 * types[i].names.size(), 4);
    }
    // Name directories, grouped per type.
    std::vector<std::vector<size_t>> nameDirOffset(types.size());
    for (size_t i = 0; i < types.size(); ++i) {
        nameDirOffset[i].resize(types[i].names.size());
        for (size_t j = 0; j < types[i].names.size(); ++j) {
            nameDirOffset[i][j] = cursor;
            cursor = align_up(cursor + 16 + 8 * types[i].names[j].leaves.size(), 4);
        }
    }
    std::vector<size_t> dataEntryOffset;
    for (const TypeGroup& tg : types)
        for (const NameGroup& ng : tg.names)
            for (size_t k = 0; k < ng.leaves.size(); ++k) {
                dataEntryOffset.push_back(cursor);
                cursor += 16;
            }

    // Name strings.
    std::map<std::string, size_t> typeStringOffset;
    std::map<std::string, size_t> nameStringOffset;
    for (const TypeGroup& tg : types) {
        if (!tg.key.named) continue;
        typeStringOffset[tg.key.str] = cursor;
        cursor += 2 + tg.key.str.size() * 2;
    }
    for (const TypeGroup& tg : types)
        for (const NameGroup& ng : tg.names) {
            if (!ng.key.named) continue;
            nameStringOffset[ng.key.str] = cursor;
            cursor += 2 + ng.key.str.size() * 2;
        }
    cursor = align_up(cursor, 4);

    // Blob payloads.
    std::vector<size_t> blobOffset;
    for (const TypeGroup& tg : types)
        for (const NameGroup& ng : tg.names)
            for (size_t k = 0; k < ng.leaves.size(); ++k) {
                blobOffset.push_back(cursor);
                cursor += align_up(ng.leaves[k].leaf->data.size(), 4);
            }

    Bytes section(cursor, 0);

    auto writeDirHeader = [&](size_t off, size_t entryCount, size_t namedCount) {
        wr32(section, off, 0);
        wr32(section, off + 4, 0);
        wr16(section, off + 8, 0);
        wr16(section, off + 10, 0);
        wr16(section, off + 12, static_cast<uint16_t>(namedCount));
        wr16(section, off + 14, static_cast<uint16_t>(entryCount - namedCount));
    };
    auto writeNameString = [&](size_t off, const std::string& s) {
        wr16(section, off, static_cast<uint16_t>(s.size()));
        for (size_t i = 0; i < s.size(); ++i) {
            uint16_t c = static_cast<uint8_t>(s[i]);
            wr16(section, off + 2 + i * 2, c);
        }
    };
    auto entryNameField = [&](bool named, uint16_t id, const std::string& str,
                              const std::map<std::string, size_t>& offsets) -> uint32_t {
        if (!named) return id;
        return 0x80000000u | static_cast<uint32_t>(offsets.at(str));
    };

    // Root directory.
    writeDirHeader(0, types.size(), 0);
    for (size_t i = 0; i < types.size(); ++i) {
        size_t e = 16 + i * 8;
        wr32(section, e, entryNameField(types[i].key.named, types[i].key.id, types[i].key.str,
                                        typeStringOffset));
        wr32(section, e + 4, 0x80000000u | static_cast<uint32_t>(typeDirOffset[i]));
    }
    for (const TypeGroup& tg : types) {
        if (!tg.key.named) continue;
        writeNameString(typeStringOffset.at(tg.key.str), tg.key.str);
    }

    // Type directories.
    for (size_t i = 0; i < types.size(); ++i) {
        const TypeGroup& tg = types[i];
        size_t off = typeDirOffset[i];
        size_t named = 0;
        for (const NameGroup& ng : tg.names)
            if (ng.key.named) ++named;
        writeDirHeader(off, tg.names.size(), named);
        for (size_t j = 0; j < tg.names.size(); ++j) {
            size_t e = off + 16 + j * 8;
            wr32(section, e, entryNameField(tg.names[j].key.named, tg.names[j].key.id,
                                            tg.names[j].key.str, nameStringOffset));
            wr32(section, e + 4, 0x80000000u | static_cast<uint32_t>(nameDirOffset[i][j]));
        }
    }
    for (const TypeGroup& tg : types)
        for (const NameGroup& ng : tg.names) {
            if (!ng.key.named) continue;
            writeNameString(nameStringOffset.at(ng.key.str), ng.key.str);
        }

    // Name directories and data entries.
    size_t dataIndex = 0;
    for (size_t i = 0; i < types.size(); ++i) {
        const TypeGroup& tg = types[i];
        for (size_t j = 0; j < tg.names.size(); ++j) {
            const NameGroup& ng = tg.names[j];
            size_t off = nameDirOffset[i][j];
            writeDirHeader(off, ng.leaves.size(), 0);
            for (size_t k = 0; k < ng.leaves.size(); ++k) {
                size_t e = off + 16 + k * 8;
                wr32(section, e, ng.leaves[k].language);
                wr32(section, e + 4, static_cast<uint32_t>(dataEntryOffset[dataIndex]));

                size_t de = dataEntryOffset[dataIndex];
                wr32(section, de, base_rva + static_cast<uint32_t>(blobOffset[dataIndex]));
                wr32(section, de + 4, static_cast<uint32_t>(ng.leaves[k].leaf->data.size()));
                wr32(section, de + 8, 0);
                wr32(section, de + 12, 0);

                const Bytes& payload = ng.leaves[k].leaf->data;
                if (!payload.empty())
                    std::memcpy(section.data() + blobOffset[dataIndex], payload.data(), payload.size());
                ++dataIndex;
            }
        }
    }

    return section;
}

namespace {

// Rebuilds the image with one extra section appended, preserving section
// contents and the trailing overlay. Returns the RVA the new section lives
// at.
bool append_section(Bytes& image, const Bytes& payload, const char* sectionName,
                    uint32_t& newRva, std::string& err) {
    PeImage pe;
    if (!pe_parse(image, pe, err)) return false;
    if (pe.numberOfSections == 0) {
        err = "image has no sections";
        return false;
    }

    uint32_t fileAlignment = pe.fileAlignment ? pe.fileAlignment : 0x200;
    uint32_t sectionAlignment = pe.sectionAlignment ? pe.sectionAlignment : 0x1000;

    size_t tableEnd = pe.sectionTableOffset + static_cast<size_t>(pe.numberOfSections) * 40;
    size_t newTableEnd = tableEnd + 40;
    uint32_t newSizeOfHeaders = pe.sizeOfHeaders;
    if (newTableEnd > newSizeOfHeaders) {
        newSizeOfHeaders = static_cast<uint32_t>(align_up(static_cast<uint32_t>(newTableEnd), fileAlignment));
    }

    // Order sections by their raw offset so the rebuilt file stays tidy.
    std::vector<size_t> order(pe.sections.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return pe.sections[a].pointerToRawData < pe.sections[b].pointerToRawData;
    });

    // Overlay: everything after the last section's raw data.
    uint32_t lastRawEnd = 0;
    for (const PeSection& s : pe.sections)
        lastRawEnd = std::max(lastRawEnd, s.pointerToRawData + s.sizeOfRawData);
    Bytes overlay;
    if (lastRawEnd < image.size())
        overlay.assign(image.begin() + lastRawEnd, image.end());

    Bytes rebuilt(newSizeOfHeaders, 0);
    std::memcpy(rebuilt.data(), image.data(), std::min<size_t>(pe.sizeOfHeaders, image.size()));

    uint32_t cursor = newSizeOfHeaders;
    std::vector<uint32_t> newRaw(pe.sections.size(), 0);
    for (size_t idx : order) {
        const PeSection& s = pe.sections[idx];
        if (s.sizeOfRawData == 0) {
            newRaw[idx] = cursor;
            continue;
        }
        cursor = align_up(cursor, fileAlignment);
        newRaw[idx] = cursor;
        if (s.pointerToRawData + s.sizeOfRawData > image.size()) {
            err = "section data out of bounds";
            return false;
        }
        if (rebuilt.size() < cursor + s.sizeOfRawData) rebuilt.resize(cursor + s.sizeOfRawData, 0);
        std::memcpy(rebuilt.data() + cursor, image.data() + s.pointerToRawData, s.sizeOfRawData);
        cursor += s.sizeOfRawData;
    }

    // The new section.
    uint32_t maxVaEnd = 0;
    for (const PeSection& s : pe.sections)
        maxVaEnd = std::max(maxVaEnd, s.virtualAddress + std::max(s.virtualSize, s.sizeOfRawData));
    newRva = align_up(maxVaEnd, sectionAlignment);

    cursor = align_up(cursor, fileAlignment);
    uint32_t newRawPtr = cursor;
    uint32_t newRawSize = static_cast<uint32_t>(align_up(static_cast<uint32_t>(payload.size()), fileAlignment));
    if (rebuilt.size() < static_cast<size_t>(newRawPtr) + newRawSize)
        rebuilt.resize(static_cast<size_t>(newRawPtr) + newRawSize, 0);
    if (!payload.empty()) std::memcpy(rebuilt.data() + newRawPtr, payload.data(), payload.size());
    cursor = newRawPtr + newRawSize;

    if (!overlay.empty()) {
        cursor = align_up(cursor, fileAlignment);
        if (rebuilt.size() < cursor + overlay.size()) rebuilt.resize(cursor + overlay.size(), 0);
        std::memcpy(rebuilt.data() + cursor, overlay.data(), overlay.size());
    }

    // Headers.
    wr16(rebuilt, pe.peOffset + 6, static_cast<uint16_t>(pe.numberOfSections + 1));
    wr32(rebuilt, pe.optionalOffset + 56,
         align_up(newRva + static_cast<uint32_t>(align_up(static_cast<uint32_t>(payload.size()), sectionAlignment)),
                 sectionAlignment));
    wr32(rebuilt, pe.optionalOffset + 60, newSizeOfHeaders);

    for (size_t i = 0; i < pe.sections.size(); ++i) {
        size_t o = pe.sectionTableOffset + i * 40;
        wr32(rebuilt, o + 20, newRaw[i]);
    }
    size_t newSectionHeader = pe.sectionTableOffset + static_cast<size_t>(pe.numberOfSections) * 40;
    std::memset(rebuilt.data() + newSectionHeader, 0, 40);
    std::memcpy(rebuilt.data() + newSectionHeader, sectionName, std::min<size_t>(8, std::strlen(sectionName)));
    wr32(rebuilt, newSectionHeader + 8, static_cast<uint32_t>(align_up(static_cast<uint32_t>(payload.size()), sectionAlignment)));
    wr32(rebuilt, newSectionHeader + 12, newRva);
    wr32(rebuilt, newSectionHeader + 16, newRawSize);
    wr32(rebuilt, newSectionHeader + 20, newRawPtr);
    wr32(rebuilt, newSectionHeader + 36, kSectionCharInitializedData | kSectionCharRead);

    image = std::move(rebuilt);
    return true;
}

} // namespace

bool pe_write_resources(Bytes& image, const std::vector<ResourceLeaf>& leaves, std::string& err) {
    if (leaves.empty()) return true;

    PeImage pe;
    if (!pe_parse(image, pe, err)) return false;

    std::vector<ResourceLeaf> merged;
    if (pe_has_resources(pe)) {
        if (!pe_read_resources(pe, merged, err)) return false;
    }

    // The image's own resources always win. A wrapper's resource is only
    // added when the image does not already carry that exact key, so a
    // wrapper can never shadow what the game shipped with.
    auto sameKey = [](const ResourceLeaf& a, const ResourceLeaf& b) {
        if (a.typeIsNamed != b.typeIsNamed || a.nameIsNamed != b.nameIsNamed) return false;
        if (a.typeIsNamed) { if (a.typeName != b.typeName) return false; }
        else if (a.type != b.type) return false;
        if (a.nameIsNamed) { if (a.nameString != b.nameString) return false; }
        else if (a.nameId != b.nameId) return false;
        return a.language == b.language;
    };
    for (const ResourceLeaf& leaf : leaves) {
        bool alreadyThere = false;
        for (const ResourceLeaf& existing : merged) {
            if (sameKey(existing, leaf)) { alreadyThere = true; break; }
        }
        if (!alreadyThere) merged.push_back(leaf);
    }
    if (merged.size() == (pe_has_resources(pe) ? merged.size() : 0)) {
        // nothing new to add
    }

    // If the image already owns a resource section that can hold the new
    // tree, write it in place. Otherwise append a fresh section.
    for (const PeSection& s : pe.sections) {
        if (s.name != ".rsrc") continue;
        Bytes trial = build_resource_section(merged, s.virtualAddress);
        if (trial.size() <= s.sizeOfRawData) {
            if (image.size() < static_cast<size_t>(s.pointerToRawData) + trial.size())
                image.resize(static_cast<size_t>(s.pointerToRawData) + trial.size(), 0);
            std::memset(image.data() + s.pointerToRawData, 0, s.sizeOfRawData);
            std::memcpy(image.data() + s.pointerToRawData, trial.data(), trial.size());
            uint32_t dirOffset = data_dir_offset(pe);
            wr32(image, dirOffset + kResourceDirIndex * 8, s.virtualAddress);
            wr32(image, dirOffset + kResourceDirIndex * 8 + 4, static_cast<uint32_t>(trial.size()));
            return true;
        }
        break;
    }

    // Append a new section carrying the merged tree.
    uint32_t sectionRva = 0;
    {
        PeImage probe;
        if (!pe_parse(image, probe, err)) return false;
        uint32_t maxVaEnd = 0;
        for (const PeSection& s : probe.sections)
            maxVaEnd = std::max(maxVaEnd, s.virtualAddress + std::max(s.virtualSize, s.sizeOfRawData));
        uint32_t sectionAlignment = probe.sectionAlignment ? probe.sectionAlignment : 0x1000;
        sectionRva = align_up(maxVaEnd, sectionAlignment);
    }
    Bytes tree = build_resource_section(merged, sectionRva);
    uint32_t actualRva = 0;
    if (!append_section(image, tree, ".rsrc", actualRva, err)) return false;

    // The appended section may have landed on a different RVA than predicted.
    if (actualRva != sectionRva) {
        tree = build_resource_section(merged, actualRva);
        PeImage after;
        if (!pe_parse(image, after, err)) return false;
        for (const PeSection& s : after.sections) {
            if (s.virtualAddress != actualRva) continue;
            if (tree.size() > s.sizeOfRawData) {
                err = "resource section does not fit after relocation";
                return false;
            }
            std::memset(image.data() + s.pointerToRawData, 0, s.sizeOfRawData);
            std::memcpy(image.data() + s.pointerToRawData, tree.data(), tree.size());
            break;
        }
    }

    PeImage final;
    if (!pe_parse(image, final, err)) return false;
    uint32_t dirOffset = data_dir_offset(final);
    wr32(image, dirOffset + kResourceDirIndex * 8, actualRva);
    wr32(image, dirOffset + kResourceDirIndex * 8 + 4, static_cast<uint32_t>(tree.size()));
    return true;
}

bool pe_has_icon_group(const PeImage& pe, std::string& err) {
    if (!pe_has_resources(pe)) return false;
    std::vector<ResourceLeaf> leaves;
    if (!pe_read_resources(pe, leaves, err)) return false;
    for (const ResourceLeaf& leaf : leaves)
        if (!leaf.typeIsNamed && leaf.type == kRtGroupIcon) return true;
    return false;
}

bool pe_add_icon(Bytes& image, const IconSet& icon, std::string& err) {
    if (icon.group.size() < 6 || icon.images.empty()) return false;

    PeImage pe;
    if (!pe_parse(image, pe, err)) return false;

    std::vector<ResourceLeaf> leaves;
    if (pe_has_resources(pe) && !pe_read_resources(pe, leaves, err)) return false;

    // The game already has an icon of its own. It always wins: the shell
    // looks for the group called "MAINICON" first, so a wrapper's group
    // would shadow the game's and the game would lose its own artwork.
    for (const ResourceLeaf& leaf : leaves)
        if (!leaf.typeIsNamed && leaf.type == kRtGroupIcon) return false;

    // Find a free range of icon ids and remap the group onto it.
    std::set<uint16_t> used;
    uint16_t groupIdTaken = 0;
    for (const ResourceLeaf& leaf : leaves) {
        if (leaf.typeIsNamed) continue;
        if (leaf.type == kRtIcon && !leaf.nameIsNamed) used.insert(leaf.nameId);
        if (leaf.type == kRtGroupIcon && !leaf.nameIsNamed) groupIdTaken = leaf.nameId;
    }

    std::map<uint16_t, uint16_t> mapping;
    std::vector<ResourceLeaf> newIcons;
    uint16_t next = 1;
    for (const auto& img : icon.images) {
        while (used.count(next)) ++next;
        used.insert(next);
        mapping[img.first] = next;
        ResourceLeaf leaf;
        leaf.type = kRtIcon;
        leaf.nameId = next;
        leaf.language = icon.language;
        leaf.data = img.second;
        newIcons.push_back(leaf);
    }

    // Rewrite the group so it points at the remapped ids.
    Bytes group = icon.group;
    uint16_t count = static_cast<uint16_t>(group[4] | (group[5] << 8));
    for (uint16_t i = 0; i < count; ++i) {
        size_t off = 6 + static_cast<size_t>(i) * 14;
        if (off + 14 > group.size()) break;
        uint16_t oldId = static_cast<uint16_t>(group[off + 12] | (group[off + 13] << 8));
        auto it = mapping.find(oldId);
        uint16_t newId = (it != mapping.end()) ? it->second : 0;
        group[off + 12] = static_cast<uint8_t>(newId & 0xFF);
        group[off + 13] = static_cast<uint8_t>((newId >> 8) & 0xFF);
    }

    // Register the group under the name the shell looks for first, and
    // under id 1 as well when that id is free.
    ResourceLeaf named;
    named.type = kRtGroupIcon;
    named.nameIsNamed = true;
    named.nameString = "MAINICON";
    named.language = icon.language;
    named.data = group;
    newIcons.push_back(named);

    if (groupIdTaken == 0) {
        ResourceLeaf byId;
        byId.type = kRtGroupIcon;
        byId.nameId = 1;
        byId.language = icon.language;
        byId.data = group;
        newIcons.push_back(byId);
    }

    return pe_write_resources(image, newIcons, err);
}

bool pe_looks_like_program(const Bytes& data, std::string& err) {
    PeImage pe;
    if (!pe_parse(data, pe, err)) return false;
    if (pe.numberOfSections == 0) {
        err = "image has no sections";
        return false;
    }
    bool entryFound = false;
    for (const PeSection& s : pe.sections) {
        uint32_t span = std::max(s.virtualSize, s.sizeOfRawData);
        if (pe.addressOfEntryPoint >= s.virtualAddress &&
            pe.addressOfEntryPoint < s.virtualAddress + span) {
            entryFound = true;
            break;
        }
    }
    if (!entryFound) {
        err = "entry point is outside every section";
        return false;
    }
    if (pe.sizeOfImage == 0) {
        err = "image size is zero";
        return false;
    }
    return true;
}

} // namespace rawr
