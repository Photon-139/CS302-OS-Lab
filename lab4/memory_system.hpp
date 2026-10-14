#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "page_table.hpp"
#include "two_level_pt.hpp"
#include "single_level_pt.hpp"
#include "frame_manager.hpp"
#include "bump_frame_manager.hpp"
#include "lru_frame_manager.hpp"
#include "tlb.hpp"

enum class PageTableVariant {SINGLE, TWOLEVEL};

// Everything that selects how the simulator is built. Defaults are Part I:
// single-level table, enough frames that nothing is evicted, no TLB.
struct MemoryConfig{
    PageTableVariant page_table = PageTableVariant::SINGLE;
    int num_frames = NUM_PAGES;     // below NUM_PAGES, frames run out and LRU replacement is used
    bool use_tlb = false;
};

// Everything the report needs, filled in as accesses are simulated.
struct MemoryStats{
    uint64_t accesses = 0;
    uint64_t cycles = 0;
    uint64_t tlb_hits = 0;          // the TLB is looked up on every access, so misses = accesses - hits
    uint64_t page_faults = 0;       // every first load of a page, including into a free frame
    uint64_t evictions = 0;         // faults that had to go to disk
    uint64_t hash = 0;              // H = f(H, frame_number), updated on every access
};

class MemorySystem{
    static constexpr int TLB_ENTRIES = 128;
    static constexpr int TLB_COST = 1;
    static constexpr int MEM_COST = 200;
    static constexpr int DISK_COST = 20000000;

    std::unique_ptr<PageTable> page_table;
    std::unique_ptr<FrameManager> frame_manager;
    std::unique_ptr<TLB> tlb;           // null when the TLB is disabled
    std::vector<uint32_t> vpn_accesses; // accesses per virtual page, NUM_PAGES entries
    MemoryStats stats;

    int handle_page_fault(int vpn);

public:
    explicit MemorySystem(const MemoryConfig& config = MemoryConfig());

    // Simulates one access to a 32-bit virtual address.
    void memory_access(uint32_t address);

    const MemoryStats& get_stats() const;

    // Stat A: distinct frames used (peak).
    int frames_used() const;

    // Stat D: pages touched / page table entries allocated.
    double page_table_utilization() const;

    // Stat B: per-page access counts, for the hot-pages plot.
    const std::vector<uint32_t>& page_access_counts() const;
};
