#include "stats.hpp"
#include <chrono>
#include <cstdio>
#include <string>

uint64_t now_ns()
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- Stage statistics ----

stage_stats& this_stage()
{
    thread_local stage_stats stats;
    return stats;
}

void print_stage_stats(const char* stage, uint64_t start_ns)
{
    const stage_stats& s = this_stage();
    std::string line = std::string("stage=") + stage
                     + " busy="     + std::to_string(s.busy_ns / 1000)
                     + " wait_in="  + std::to_string(s.wait_in_ns / 1000)
                     + " wait_out=" + std::to_string(s.wait_out_ns / 1000)
                     + " verify="   + std::to_string(s.verify_ns / 1000);
    if(s.first_in_ns != 0){
        line += " first_in=" + std::to_string((s.first_in_ns - start_ns) / 1000);
    }
    line += "\n";
    fputs(line.c_str(), stdout);   // one call: lines from different threads don't interleave
    fflush(stdout);
}

void* run_stage(void* arg)
{
    stage_launch* launch = static_cast<stage_launch*>(arg);
    launch->func(launch->ctx);
    print_stage_stats(launch->name, launch->start_ns);
    return nullptr;
}

// ---- Link statistics ----

void link_stats::add(const strip_hdr& hdr, size_t payload, size_t wire)
{
    strips        += 1;
    payload_bytes += payload;
    wire_bytes    += wire;
    stream         = fnv1a(stream, hdr.checksum);
}

void print_link_stats(const char* link, const char* side, const link_stats& s, bool show_wire)
{
    char hash[17];
    snprintf(hash, sizeof(hash), "%016llx", (unsigned long long)s.stream);

    std::string line = std::string("link=") + link
                     + " side=" + side
                     + " strips=" + std::to_string(s.strips)
                     + " payload_bytes=" + std::to_string(s.payload_bytes);
    if(show_wire){
        line += " wire_bytes=" + std::to_string(s.wire_bytes);
    }
    line += std::string(" stream=") + hash + "\n";
    fputs(line.c_str(), stdout);
    fflush(stdout);
}

// ---- Fault injection ----

#ifdef FAULT_INJECT
static constexpr uint64_t FAULT_STRIP = 5;   // the 5th strip a thread sends gets broken
#endif

void maybe_inject_fault(strip_hdr& hdr, uint8_t* pixels)
{
#ifdef FAULT_INJECT
    thread_local uint64_t sent = 0;
    if(++sent != FAULT_STRIP) return;
#if FAULT_INJECT == 1
    pixels[0] ^= 0x01;                       // content no longer matches the checksum
    fprintf(stderr, "FAULT_INJECT: flipped one pixel byte of strip (iteration=%u, r0=%u)\n",
            hdr.iteration, hdr.r0);
#elif FAULT_INJECT == 2
    fprintf(stderr, "FAULT_INJECT: changed r0 of strip (iteration=%u, r0=%u) to r0=%u\n",
            hdr.iteration, hdr.r0, hdr.r0 + hdr.nrows);
    hdr.r0 += hdr.nrows;                     // claims to be the next strip: arrives out of order
#else
#error "FAULT_INJECT must be 1 (corrupt a byte) or 2 (out of order)"
#endif
#else
    (void)hdr;
    (void)pixels;
#endif
}
