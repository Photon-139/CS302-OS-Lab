// spsc_ring.hpp
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include "strip.hpp"


constexpr int SPINS = 1024;

// Single-producer / single-consumer ring of N strip slots.
// Exactly one thread may call acquire()/publish(), and exactly one other thread receive()/release().
class spsc_ring {
public:
    spsc_ring(size_t capacity, int k, int width);   // allocates N headers + N × slot_bytes pixels; counters = 0
    ~spsc_ring();                                    // frees both arrays; call only after all threads have joined

    spsc_ring(const spsc_ring&)            = delete;
    spsc_ring& operator=(const spsc_ring&) = delete;

    // Producer
    strip_view acquire();   // waits while full (head − tail == N); returns slot head % N
    void       publish();   // head + 1 (release): slot becomes visible to the consumer

    // Consumer
    strip_view receive();   // waits while empty (head == tail); returns slot tail % N
    void       release();   // tail + 1 (release): slot can be reused by the producer

    size_t capacity() const;
    size_t slot_bytes() const;

private:
    size_t     capacity_;     // N
    size_t     slot_bytes_;   // k × width × 3
    strip_hdr* headers_;      // N headers
    uint8_t*   pixels_;       // N × slot_bytes_

    alignas(64) std::atomic<uint64_t> head_{0};   // slots published so far; written only by the producer
    alignas(64) std::atomic<uint64_t> tail_{0};   // slots released so far; written only by the consumer
};
