#include "strip.hpp"
#include "stats.hpp"
#include "libppm.hpp"
#include <cstdio>
#include <cstdlib>
#include <arpa/inet.h>
#include <cstring>

static constexpr uint64_t FNV_PRIME = 0x100000001b3ULL;

// FNV-1a over a byte range, continuing from h.
static uint64_t fnv1a_bytes(uint64_t h, const uint8_t* p, size_t n)
{
    for(size_t i = 0; i < n; ++i){
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

uint64_t fnv1a(uint64_t starting_value, uint64_t value)
{
    // Byte-by-byte from least significant, so the result does not depend on host byte order.
    uint64_t h = starting_value;
    for(int i = 0; i < 8; ++i){
        h ^= (value >> (8 * i)) & 0xff;
        h *= FNV_PRIME;
    }
    return h;
}

uint64_t strip_checksum(const strip_hdr& hdr, const uint8_t* pixels, size_t n)
{
    scoped_timer timer(this_stage().verify_ns);   // stage statistics: sealing / checking cost
    // Fields are hashed individually (never the raw struct), so padding or the
    // checksum field itself can never influence the result.
    uint64_t h = FNV_OFFSET_BASIS;
    h = fnv1a(h, hdr.iteration);
    h = fnv1a(h, hdr.r0);
    h = fnv1a(h, hdr.nrows);
    h = fnv1a(h, hdr.flags);
    return fnv1a_bytes(h, pixels, n);
}

void verify_strip(const strip_hdr& hdr, const uint8_t* pixels, int width,
                  uint32_t expected_iteration, uint32_t expected_r0, uint32_t expected_nrows)
{
    if(hdr.iteration != expected_iteration || hdr.r0 != expected_r0 || hdr.nrows != expected_nrows){
        fprintf(stderr,
                "strip order error: expected (iteration=%u, r0=%u, nrows=%u), "
                "received (iteration=%u, r0=%u, nrows=%u)\n",
                expected_iteration, expected_r0, expected_nrows,
                hdr.iteration, hdr.r0, hdr.nrows);
        abort();
    }

    // Size comes from the (now validated) header, so it cannot disagree with it.
    size_t n = (size_t)hdr.nrows * width * CHANNELS;
    uint64_t computed = strip_checksum(hdr, pixels, n);
    if(computed != hdr.checksum){
        fprintf(stderr,
                "strip checksum mismatch at (iteration=%u, r0=%u, nrows=%u): "
                "header says %016llx, payload gives %016llx\n",
                hdr.iteration, hdr.r0, hdr.nrows,
                (unsigned long long)hdr.checksum, (unsigned long long)computed);
        abort();
    }
}

void encode_header(const strip_hdr& h, uint8_t out[WIRE_HDR_BYTES])
{
    const uint32_t words[6] = {
        htonl(h.iteration), htonl(h.r0), htonl(h.nrows), htonl(h.flags),
        htonl((uint32_t)(h.checksum >> 32)),          // high half
        htonl((uint32_t)(h.checksum & 0xffffffffu)),  // low half
    };
    std::memcpy(out, words, WIRE_HDR_BYTES);
}

void decode_header(const uint8_t in[WIRE_HDR_BYTES], strip_hdr& h)
{
    uint32_t words[6];
    std::memcpy(words, in, WIRE_HDR_BYTES);
    h.iteration = ntohl(words[0]);
    h.r0        = ntohl(words[1]);
    h.nrows     = ntohl(words[2]);
    h.flags     = ntohl(words[3]);
    h.checksum  = ((uint64_t)ntohl(words[4]) << 32) | ntohl(words[5]);
}

uint64_t image_checksum(const flat_image_t* image)
{
    return fnv1a_bytes(FNV_OFFSET_BASIS, image->image_pixels,
                       (size_t)image->width * image->height * CHANNELS);
}
