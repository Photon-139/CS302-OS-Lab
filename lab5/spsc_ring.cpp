#include "spsc_ring.hpp"
#include <immintrin.h>
#include <sched.h>

spsc_ring::spsc_ring(size_t capacity, int k, int width):
    capacity_(capacity), 
    slot_bytes_(k*width*3),
    headers_(new strip_hdr[capacity]),
    pixels_(new uint8_t[capacity*slot_bytes_])
{
}

spsc_ring::~spsc_ring(){
    delete[] headers_;
    delete[] pixels_;
}
static inline void backoff(unsigned& spins)
{
    if (spins < SPINS) {
        _mm_pause();      // hint to the CPU: we're in a spin-wait loop
        ++spins;
    } else {
        sched_yield();    // let another thread run on this core
    }
}

strip_view spsc_ring::acquire()
{
    // Our own counter: only this (producer) thread ever writes head_, so relaxed is enough.
    const uint64_t head = head_.load(std::memory_order_relaxed);

    // The consumer's counter: acquire, so that once we see the slot has been released,
    // all of the consumer's reads of that slot are finished before we overwrite it.
    unsigned spins = 0;
    while (head - tail_.load(std::memory_order_acquire) == capacity_) {
        backoff(spins);   // ring is full: wait for the consumer to release a slot
    }

    const size_t slot = head % capacity_;
    return strip_view{ &headers_[slot], pixels_ + slot * slot_bytes_ };
}

void spsc_ring::publish()
{
    // Own counter: only the producer writes head_, so relaxed is enough to read it.
    const uint64_t head = head_.load(std::memory_order_relaxed);

    // Release: every write to the slot (pixels, header, checksum) is made visible
    // to the consumer before it can observe the new head_.
    head_.store(head + 1, std::memory_order_release);
}

strip_view spsc_ring::receive()
{
    // Our own counter: only this (consumer) thread ever writes tail_, so relaxed is enough.
    const uint64_t tail = tail_.load(std::memory_order_relaxed);

    // The producer's counter: acquire, so that once we see the slot has been published,
    // all of the producer's writes to it (pixels, header, checksum) are visible to us.
    unsigned spins = 0;
    while (head_.load(std::memory_order_acquire) == tail) {
        backoff(spins);   // ring is empty: wait for the producer to publish a slot
    }

    const size_t slot = tail % capacity_;
    return strip_view{ &headers_[slot], pixels_ + slot * slot_bytes_ };
}

void spsc_ring::release()
{
    // Own counter: only the consumer writes tail_, so relaxed is enough to read it.
    const uint64_t tail = tail_.load(std::memory_order_relaxed);

    // Release: all our reads of the slot are complete before the producer can observe
    // the new tail_ and start overwriting the slot.
    tail_.store(tail + 1, std::memory_order_release);
}

size_t spsc_ring::capacity() const{
    return capacity_;
}
size_t spsc_ring::slot_bytes() const{
    return slot_bytes_;
}