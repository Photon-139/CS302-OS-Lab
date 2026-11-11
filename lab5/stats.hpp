#pragma once
#include <cstddef>
#include <cstdint>
#include "strip.hpp"

// Measurements for the report. Two kinds:
//
//  * Stage statistics: where each stage spends its time. Every stage runs in its own thread
//    (or process), so the counters are thread_local and need no synchronisation. They are
//    filled in automatically: busy time by the S1/S2/S3 kernels, verify time by
//    strip_checksum(), wait times and the first arrival by the channels.
//
//  * Link statistics: what went through one link (S1->S2 or S2->S3), counted by the channel
//    on each side: in publish() by the sender, in release() by the receiver (a stage only
//    releases a strip after verify_strip() accepted it). `stream` folds the checksums of all
//    strips in order; equal values on both sides mean the receiver accepted exactly the
//    strips the sender sent, with the same content, in the same order.
//
// All times are printed in microseconds.

// Monotonic clock in nanoseconds (steady_clock: comparable across threads and processes
// on the same machine).
uint64_t now_ns();

// ---- Stage statistics ----

struct stage_stats {
    uint64_t busy_ns     = 0;    // in the S1/S2/S3 kernels
    uint64_t verify_ns   = 0;    // in strip_checksum (sealing on send, checking on receive)
    uint64_t wait_in_ns  = 0;    // in receive(): waiting for data (pipes/sockets: incl. transfer)
    uint64_t wait_out_ns = 0;    // in acquire()/publish(): waiting for space (pipes/sockets: incl. transfer)
    uint64_t first_in_ns = 0;    // now_ns() when the first strip arrived; 0 = never received
};

// The calling thread's statistics.
stage_stats& this_stage();

// Print the calling thread's statistics as one line, e.g.
//   stage=S2 busy=... wait_in=... wait_out=... verify=... first_in=...
// first_in is relative to start_ns and only printed if the stage received anything.
void print_stage_stats(const char* stage, uint64_t start_ns);

// Adds the time since construction to a counter (used inside kernels and channels).
struct scoped_timer {
    uint64_t& counter;
    uint64_t  start;
    explicit scoped_timer(uint64_t& c) : counter(c), start(now_ns()) {}
    ~scoped_timer() { counter += now_ns() - start; }
};

// Thread entry point that runs one stage and then prints its statistics from that thread.
struct stage_launch {
    void* (*func)(void*);   // the stage function (e.g. s2_variant1)
    void*       ctx;        // its argument (the pipeline context)
    const char* name;       // "S1", "S2" or "S3"
    uint64_t    start_ns;   // start of the timed region, for first_in
};
void* run_stage(void* launch);   // pass a stage_launch*; usable with pthread_create

// ---- Link statistics ----

struct link_stats {
    uint64_t strips        = 0;
    uint64_t payload_bytes = 0;                  // pixel bytes
    uint64_t wire_bytes    = 0;                  // payload + headers actually written/read (pipes, sockets)
    uint64_t stream        = FNV_OFFSET_BASIS;   // running hash over the strips' checksums, in order

    void add(const strip_hdr& hdr, size_t payload, size_t wire);
};

// One line, e.g.  link=S1->S2 side=send strips=... payload_bytes=... [wire_bytes=...] stream=...
// wire_bytes is printed only for byte-stream channels (show_wire).
void print_link_stats(const char* link, const char* side, const link_stats& s, bool show_wire);

// ---- Fault injection (only compiled with -DFAULT_INJECT=1 or 2) ----

// Called by every channel's publish(), after the strip is sealed. On the FAULT_STRIP-th strip
// a thread sends, it breaks the strip so the receiver's verify_strip() must catch it:
//   FAULT_INJECT=1: flip one pixel byte        -> "strip checksum mismatch"
//   FAULT_INJECT=2: give it the wrong row (r0)  -> "strip order error" (as if out of order)
// Without FAULT_INJECT this does nothing.
void maybe_inject_fault(strip_hdr& hdr, uint8_t* pixels);
