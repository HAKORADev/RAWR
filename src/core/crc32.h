// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) - the same function the
// Reflexive wrapper uses to derive its stream seeds. Self-contained so the
// binary carries no external dependency.
#pragma once

#include <cstdint>
#include <cstddef>
#include "common.h"

namespace rawr {

uint32_t crc32_update(uint32_t crc, const void* data, size_t len);
uint32_t crc32_of(const void* data, size_t len);

} // namespace rawr
