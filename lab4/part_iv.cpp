// Part IV: limited physical memory with LRU replacement (TLB enabled).
// Physical memory is 1/2^h of the virtual address space for the h values below. The benchmarks
// only touch ~100 to ~14000 pages, so the list reaches far down to where replacement starts.
//   fault_rate    = every first load of a page (including into a free frame) / accesses
//   eviction_rate = loads that had to replace a page (the only ones that cost disk time) / accesses
// Prints one CSV row per run. Usage: part_iv [trace_dir] [max_accesses]
#include "benchmark.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv){
    std::string trace_dir = argc > 1 ? argv[1] : "traces";
    uint64_t max_accesses = argc > 2 ? strtoull(argv[2], nullptr, 10) : 0;

    const int halvings[] = {1, 6, 8, 10, 12, 14, 16, 18};

    printf("benchmark,page_table,memory_fraction,frames,accesses,page_faults,evictions,"
           "fault_rate,eviction_rate,tlb_hit_rate,cycles,seconds\n");
    for(const std::string& trace : list_traces(trace_dir)){
        for(PageTableVariant variant : {PageTableVariant::SINGLE, PageTableVariant::TWOLEVEL}){
            for(int h : halvings){
                MemoryConfig config;
                config.page_table = variant;
                config.use_tlb = true;
                config.num_frames = NUM_PAGES >> h;

                BenchmarkResult r = run_benchmark(trace, config, max_accesses);
                double accesses = r.stats.accesses ? static_cast<double>(r.stats.accesses) : 1.0;
                printf("%s,%s,%.10f,%d,%llu,%llu,%llu,%.8f,%.8f,%.6f,%llu,%.3f\n", r.name.c_str(),
                       variant == PageTableVariant::SINGLE ? "single" : "two", 1.0 / (1 << h),
                       config.num_frames, (unsigned long long)r.stats.accesses,
                       (unsigned long long)r.stats.page_faults, (unsigned long long)r.stats.evictions,
                       r.stats.page_faults / accesses, r.stats.evictions / accesses,
                       tlb_hit_rate(r.stats), (unsigned long long)r.stats.cycles, r.seconds);
            }
        }
    }
}
