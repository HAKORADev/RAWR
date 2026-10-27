#include "stream_cipher.h"

namespace rawr {

void stream_init(uint32_t seed, uint32_t state[kStateWords], int& index_a, int& index_b) {
    uint32_t current = seed & kMask32;

    // Backwards LCG fill: the seed feeds the first multiplication and the
    // high half of each product becomes the next state word.
    for (int index = kStateWords - 1; index >= 0; --index) {
        uint64_t product = static_cast<uint64_t>(current) * 0x41C64E6Dull + 0x3039ull;
        current = static_cast<uint32_t>(product & 0xFFFFFFFFull);
        state[index] = static_cast<uint32_t>((product >> 16) & 0xFFFFFFFFull);
    }

    // Mersenne-Twister style seed fingerprint: one bit every seven words.
    uint32_t clear_mask = kMask32;
    uint32_t bit = 0x80000000u;
    int index = 3;
    while (bit != 0) {
        state[index] = (state[index] & clear_mask) | bit;
        clear_mask >>= 1;
        bit >>= 1;
        index += 7;
    }

    index_a = 0;
    index_b = 0x67;
}

namespace {

inline uint8_t stream_next_byte(uint32_t state[kStateWords], int& index_a, int& index_b) {
    state[index_a] ^= state[index_b];
    uint8_t value = static_cast<uint8_t>(state[index_a] & 0xFF);
    index_a = (index_a + 1) % kStateWords;
    index_b = (index_b + 1) % kStateWords;
    return value;
}

} // namespace

void stream_decrypt_inplace(uint8_t* data, size_t len, uint32_t seed) {
    uint32_t state[kStateWords];
    int a = 0, b = 0;
    stream_init(seed, state, a, b);
    for (size_t i = 0; i < len; ++i) {
        uint8_t key = stream_next_byte(state, a, b);
        data[i] = static_cast<uint8_t>(data[i] - key);
    }
}

Bytes stream_decrypt_copy(const uint8_t* data, size_t len, uint32_t seed) {
    Bytes out(data, data + len);
    if (len) stream_decrypt_inplace(out.data(), len, seed);
    return out;
}

} // namespace rawr
