#include "integrated.h"

#include <cstring>

#include "pe.h"

namespace rawr {
namespace {

const char kExportName[] = "radll_HasTheProductBeenPurchased";
const uint8_t kReturnOne[6] = {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3};

// Finds a null terminated string that starts at a string boundary.
int find_standalone(const Bytes& data, const char* needle) {
    size_t n = std::strlen(needle) + 1;
    size_t pos = 0;
    while (pos + n <= data.size()) {
        size_t i = data.size();
        for (size_t k = 0; k + n <= data.size(); ++k) {
            if (std::memcmp(data.data() + k, needle, n) == 0) { i = k; break; }
        }
        if (i == data.size()) break;
        if (i == 0 || data[i - 1] == 0) return static_cast<int>(i);
        pos = i + 1;
    }
    return -1;
}

bool is_exec_section(const PeSection& s) { return (s.characteristics & 0x20000000u) != 0; }

} // namespace

bool patch_mark_registered(Bytes& image, IntegratedPatchResult& result, std::string& err) {
    result = IntegratedPatchResult();

    PeImage pe;
    if (!pe_parse(image, pe, err)) return false;

    int strOff = find_standalone(image, kExportName);
    if (strOff < 0) {
        err = "the executable does not resolve radll_HasTheProductBeenPurchased";
        return false;
    }
    uint32_t strRva = 0;
    bool found = false;
    for (const PeSection& s : pe.sections) {
        uint32_t span = s.sizeOfRawData ? s.sizeOfRawData : s.virtualSize;
        if (static_cast<uint32_t>(strOff) >= s.pointerToRawData &&
            static_cast<uint32_t>(strOff) < s.pointerToRawData + span) {
            strRva = s.virtualAddress + (static_cast<uint32_t>(strOff) - s.pointerToRawData);
            found = true;
            break;
        }
    }
    if (!found) {
        err = "the export name is not inside any section";
        return false;
    }
    uint32_t strVa = pe.imageBase + strRva;

    // The push of the export name, right before the GetProcAddress call.
    uint8_t pushPattern[5];
    pushPattern[0] = 0x68;
    std::memcpy(pushPattern + 1, &strVa, 4);

    uint32_t pushAt = 0;
    for (const PeSection& s : pe.sections) {
        if (!is_exec_section(s)) continue;
        if (static_cast<size_t>(s.pointerToRawData) + s.sizeOfRawData > image.size()) continue;
        const uint8_t* blob = image.data() + s.pointerToRawData;
        for (uint32_t i = 0; i + 5 <= s.sizeOfRawData; ++i) {
            if (std::memcmp(blob + i, pushPattern, 5) == 0) { pushAt = s.pointerToRawData + i; break; }
        }
        if (pushAt) break;
    }
    if (!pushAt) {
        err = "the export name is never pushed";
        return false;
    }

    // The resolved pointer is stored right after the call.
    uint32_t fnPtr = 0;
    const uint32_t window = 48;
    for (uint32_t i = 0; i + 6 <= window; ++i) {
        size_t at = pushAt + i;
        if (at + 6 > image.size()) break;
        uint8_t op = image[at];
        if (op == 0xA3) {  // mov dword ptr [imm32], eax
            std::memcpy(&fnPtr, image.data() + at + 1, 4);
            break;
        }
        if (op == 0x89 && image[at + 1] == 0x05) {  // mov dword ptr [imm32], eax
            std::memcpy(&fnPtr, image.data() + at + 2, 4);
            break;
        }
    }
    if (!fnPtr) {
        err = "the resolved pointer is not stored next to the call";
        return false;
    }

    // The call itself, either directly or through a thunk.
    uint8_t callPattern[6];
    uint8_t thunkPattern[6];
    callPattern[0] = 0xFF;
    callPattern[1] = 0x15;
    thunkPattern[0] = 0xFF;
    thunkPattern[1] = 0x25;
    std::memcpy(callPattern + 2, &fnPtr, 4);
    std::memcpy(thunkPattern + 2, &fnPtr, 4);

    uint32_t site = 0;
    const char* kind = "";
    for (const PeSection& s : pe.sections) {
        if (!is_exec_section(s)) continue;
        if (static_cast<size_t>(s.pointerToRawData) + s.sizeOfRawData > image.size()) continue;
        const uint8_t* blob = image.data() + s.pointerToRawData;
        for (uint32_t i = 0; i + 6 <= s.sizeOfRawData; ++i) {
            if (std::memcmp(blob + i, callPattern, 6) == 0) {
                site = s.pointerToRawData + i;
                kind = "call";
                break;
            }
            if (std::memcmp(blob + i, thunkPattern, 6) == 0) {
                site = s.pointerToRawData + i;
                kind = "thunk";
                break;
            }
        }
        if (site) break;
    }
    if (!site) {
        err = "the resolved function is never called";
        return false;
    }

    std::memcpy(image.data() + site, kReturnOne, 6);
    result.patched = true;
    result.kind = kind;
    result.fileOffset = site;
    result.detail = std::string("the ") + kind + " to radll_HasTheProductBeenPurchased now returns 1";
    return true;
}

} // namespace rawr
