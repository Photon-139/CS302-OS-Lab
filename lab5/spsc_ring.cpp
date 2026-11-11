#include "spsc_ring.hpp"
#include <immintrin.h>
#include <sched.h>

spsc_ring::spsc_ring(size_t capacity, int k, int width):
    capacity_(capacity), 
    slot_bytes_((size_t)k*width*3),
    width_(width),
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
    {
        scoped_timer timer(this_stage().wait_out_ns);   // stage statistics: waiting for space
        while (head - tail_.load(std::memory_order_acquire) == capacity_) {
            backoff(spins);   // ring is full: wait for the consumer to release a slot
        }
    }

    const size_t slot = head % capacity_;
    return strip_view{ &headers_[slot], pixels_ + slot * slot_bytes_ };
}

void spsc_ring::publish()
{
    // Own counter: only the producer writes head_, so relaxed is enough to read it.
    const uint64_t head = head_.load(std::memory_order_relaxed);

    // Link statistics, then (only in fault-injection builds) break the strip on purpose.
    const size_t slot = head % capacity_;
    sent_.add(headers_[slot], (size_t)headers_[slot].nrows * width_ * 3, 0);
    maybe_inject_fault(headers_[slot], pixels_ + slot * slot_bytes_);

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
    {
        scoped_timer timer(this_stage().wait_in_ns);    // stage statistics: waiting for data
        while (head_.load(std::memory_order_acquire) == tail) {
            backoff(spins);   // ring is empty: wait for the producer to publish a slot
        }
    }
    if (this_stage().first_in_ns == 0) this_stage().first_in_ns = now_ns();

    const size_t slot = tail % capacity_;
    return strip_view{ &headers_[slot], pixels_ + slot * slot_bytes_ };
}

void spsc_ring::release()
{
    // Own counter: only the consumer writes tail_, so relaxed is enough to read it.
    const uint64_t tail = tail_.load(std::memory_order_relaxed);

    // Link statistics: the stage releases a strip only after verify_strip() accepted it.
    const size_t slot = tail % capacity_;
    received_.add(headers_[slot], (size_t)headers_[slot].nrows * width_ * 3, 0);

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