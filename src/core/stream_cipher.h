// The Reflexive stream cipher.
//
// The Reflexive Arcade wrapper obfuscates its configuration blob and the
// game's code section with a keystream produced from a 250-word state array
// that is filled with a backwards LCG and then given the classic
// Mersenne-Twister "mag01" bit pattern. The generator itself is a plain
// XOR-shift over two rotating indices, and the keystream byte is the low
// byte of the first state word.
//
// Everything below is a byte-for-byte port of the behaviour observed in the
// shipped ReflexiveArcade.dll, so it reproduces the wrapper's own maths
// rather than approximating it.
#pragma once

#include <cstdint>
#include <cstddef>
#include "common.h"

namespace rawr {

constexpr uint32_t kMask32 = 0xFFFFFFFFu;
constexpr int kStateWords = 250;

// Builds a fresh keystream state for the given seed.
void stream_init(uint32_t seed, uint32_t state[kStateWords], int& index_a, int& index_b);

// Decrypts (the cipher is a subtraction keystream) `len` bytes in place.
void stream_decrypt_inplace(uint8_t* data, size_t len, uint32_t seed);

// Decrypts a buffer and returns the result.
Bytes stream_decrypt_copy(const uint8_t* data, size_t len, uint32_t seed);

} // namespace rawr
