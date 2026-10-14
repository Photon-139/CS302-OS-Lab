// Part IV: limited physical memory with LRU replacement (TLB enabled).
// Physical memory is 1/2^h of the virtual address space for the h values below. The benchmarks
// only touch ~100 to ~14000 pages, so the list reaches far down to where replacement starts.
//   fault_rate_%    = every first load of a page (including into a free frame) / accesses
//   eviction_rate_% = loads that had to replace a page (the only ones that cost disk time) / accesses
// Usage: part_iv [trace_dir] [max_accesses]
#include "benchmark.hpp"
#include "report_table.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv){
    std::string trace_dir = argc > 1 ? argv[1] : "traces";
    uint64_t max_accesses = argc > 2 ? strtoull(argv[2], nullptr, 10) : 0;

    const int halvings[] = {1, 6, 8, 10, 12, 14, 16, 18};

    ReportTable table("Part IV: limited physical memory with LRU replacement (TLB enabled)",
        {{"benchmark", 12}, {"page_table", 10}, {"memory", 9}, {"frames", 8}, {"faults", 10},
         {"evictions", 10}, {"fault_rate_%", 12}, {"evict_rate_%", 12}, {"tlb_hit_%", 9},
         {"cycles", 16}, {"seconds", 8}});
    for(const std::string& trace : list_traces(trace_dir)){
        for(PageTableVariant variant : {PageTableVariant::SINGLE, PageTableVariant::TWOLEVEL}){
            for(int h : halvings){
                MemoryConfig config;
                config.page_table = variant;
                config.use_tlb = true;
                config.num_frames = NUM_PAGES >> h;

                BenchmarkResult r = run_benchmark(trace, config, max_accesses);
                double accesses = r.stats.accesses ? static_cast<double>(r.stats.accesses) : 1.0;
                table.add_row({r.name, variant == PageTableVariant::SINGLE ? "single" : "two-level",
                               format("1/%d", 1 << h), format("%d", config.num_frames),
                               format("%llu", (unsigned long long)r.stats.page_faults),
                               format("%llu", (unsigned long long)r.stats.evictions),
                               format("%.4f", r.stats.page_faults / accesses * 100),
                               format("%.4f", r.stats.evictions / accesses * 100),
                               format("%.4f", tlb_hit_rate(r.stats) * 100),
                               format("%llu", (unsigned long long)r.stats.cycles), format("%.3f", r.seconds)});
            }
        }
    }
    table.write_csv("results/part_iv.csv");
}
