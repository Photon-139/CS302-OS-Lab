#pragma once
#include <cstddef>
#include <cstdint>

// One slot as seen by a stage: its header and its pixel area.
// Non-owning; valid until the matching publish()/release().

struct strip_hdr {
    uint32_t iteration;   // V1: iteration index, V2: result index
    uint32_t r0;          // first row of the strip (absolute)
    uint32_t nrows;       // k, or fewer for the last strip of an image
    uint32_t flags;       // reserved (end-of-stream etc.); keeps the header free of padding
    uint64_t checksum;    // strip_checksum() of everything above plus the payload
};
static_assert(sizeof(strip_hdr) == 24, "unexpected padding in strip_hdr");

struct strip_view {
    strip_hdr* hdr;
    uint8_t*   pixels;      // room for slot_bytes = k × width × 3 bytes
};
constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325ULL;

// Folds the 8 bytes of value into the running FNV-1a hash starting_value.
// Chain calls by passing the previous result as starting_value.
uint64_t fnv1a(uint64_t starting_value, uint64_t value);

// Checksum over the header fields (iteration, r0, nrows, flags; not checksum itself)
// followed by n payload bytes.
uint64_t strip_checksum(const strip_hdr& hdr, const uint8_t* pixels, size_t n);

// Receiver-side check. Aborts with a message unless the strip is exactly the expected one
// (iteration, r0, nrows) and its checksum matches the payload of nrows * width * 3 bytes.
void verify_strip(const strip_hdr& hdr, const uint8_t* pixels, int width,
                  uint32_t expected_iteration, uint32_t expected_r0, uint32_t expected_nrows);

constexpr size_t WIRE_HDR_BYTES = 24;

void encode_header(const strip_hdr& h, uint8_t out[WIRE_HDR_BYTES]);   // htonl on each 32-bit word
void decode_header(const uint8_t in[WIRE_HDR_BYTES], strip_hdr& h); 