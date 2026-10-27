// Minimal, dependency-free PE32/PE32+ reader and writer.
//
// RAWR only needs to do four things with a PE image:
//   * read the section table and the entry point (to locate the encrypted
//     region inside a Reflexive payload),
//   * read the resource tree (to lift the icon and manifest out of a
//     wrapper stub),
//   * write a resource tree back into an image (to give the recovered game
//     the icon it lost),
//   * report whether an image still looks like a runnable program.
//
// No external PE library is used on purpose: the shipped binary has to be a
// single self-contained file with zero system dependencies.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common.h"

namespace rawr {

struct PeSection {
    std::string name;
    uint32_t virtualSize = 0;
    uint32_t virtualAddress = 0;
    uint32_t sizeOfRawData = 0;
    uint32_t pointerToRawData = 0;
    uint32_t characteristics = 0;
};

struct PeImage {
    Bytes data;

    bool is64 = false;
    uint16_t machine = 0;
    uint16_t numberOfSections = 0;
    uint16_t sizeOfOptionalHeader = 0;

    uint32_t addressOfEntryPoint = 0;
    uint32_t imageBase = 0;
    uint32_t sectionAlignment = 0;
    uint32_t fileAlignment = 0;
    uint32_t sizeOfImage = 0;
    uint32_t sizeOfHeaders = 0;
    uint16_t subsystem = 0;
    uint32_t characteristics = 0;

    uint32_t dataDirRva[16] = {0};
    uint32_t dataDirSize[16] = {0};

    std::vector<PeSection> sections;

    uint32_t peOffset = 0;
    uint32_t optionalOffset = 0;
    uint32_t sectionTableOffset = 0;
};

enum PeResourceType : uint16_t {
    kRtCursor = 1,
    kRtBitmap = 2,
    kRtIcon = 3,
    kRtMenu = 4,
    kRtDialog = 5,
    kRtString = 6,
    kRtFontDir = 7,
    kRtFont = 8,
    kRtAccelerator = 9,
    kRtRcData = 10,
    kRtMessageTable = 11,
    kRtGroupCursor = 12,
    kRtGroupIcon = 14,
    kRtVersion = 16,
    kRtDlgInclude = 17,
    kRtPlugPlay = 19,
    kRtVxd = 20,
    kRtAniCursor = 21,
    kRtAniIcon = 22,
    kRtHtml = 23,
    kRtManifest = 24,
};

// One leaf of the resource tree: type / name / language -> payload.
struct ResourceLeaf {
    bool typeIsNamed = false;
    uint16_t type = 0;
    std::string typeName;

    bool nameIsNamed = false;
    uint16_t nameId = 0;
    std::string nameString;

    uint16_t language = 0;
    Bytes data;
};

// Parses a PE image held in memory. Returns false with a reason on failure.
bool pe_parse(const Bytes& data, PeImage& out, std::string& err);

// Maps a relative virtual address to a file offset. 0 when unmapped.
uint32_t pe_rva_to_offset(const PeImage& pe, uint32_t rva);

// True when the image carries a resource directory.
bool pe_has_resources(const PeImage& pe);

// Reads every resource leaf of the image.
bool pe_read_resources(const PeImage& pe, std::vector<ResourceLeaf>& out, std::string& err);

// Builds a complete .rsrc section payload for the given leaves.
// `base_rva` is the RVA the section will live at; the data entry RVAs are
// resolved against it.
Bytes build_resource_section(const std::vector<ResourceLeaf>& leaves, uint32_t base_rva);

// Writes the given leaves into the image as its resource directory,
// replacing whatever the image carried before. Existing resources are kept
// unless the same (type, name, language) key is overridden.
bool pe_write_resources(Bytes& image, const std::vector<ResourceLeaf>& leaves, std::string& err);

// Structural sanity check used to confirm a decrypted payload really is a
// program and not noise.
bool pe_looks_like_program(const Bytes& data, std::string& err);

} // namespace rawr
