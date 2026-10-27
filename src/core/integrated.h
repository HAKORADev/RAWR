// The "integrated wrapper" layout.
//
// A small number of Reflexive era games compile the binding layer into
// their own executable instead of shipping it as a separate stub. Such a
// game loads ReflexiveArcade.dll at run time, resolves a set of radll_*
// exports through GetProcAddress, and asks
// radll_HasTheProductBeenPurchased whether the product was bought.
//
// Important: for these games the library is load bearing. It also draws
// the frames (radll_DrawNextFrameIntoBuffer), so the support folder has
// to stay. The only thing worth changing is the answer to the purchase
// question.
//
// The patch is exact size. The call is either
//
//     FF 15 <fnptr>      call dword ptr [fnptr]
//     FF 25 <fnptr>      jmp  dword ptr [fnptr]   (a thunk the game calls)
//
// and both are six bytes, exactly the size of
//
//     B8 01 00 00 00 C3  mov eax, 1; ret
//
// so nothing moves and the stack stays balanced: the patched thunk pops
// the return address the call pushed and hands the caller 1.
#pragma once

#include <string>

#include "common.h"

namespace rawr {

struct IntegratedPatchResult {
    bool patched = false;
    std::string kind;        // "call" or "thunk"
    uint32_t fileOffset = 0;
    std::string detail;
};

// Marks the image as registered. Returns false with a reason when the
// image does not use the expected pattern, in which case nothing is
// changed.
bool patch_mark_registered(Bytes& image, IntegratedPatchResult& result, std::string& err);

} // namespace rawr
