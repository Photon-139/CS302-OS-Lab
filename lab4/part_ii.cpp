// Part II: two-level page table, no TLB, enough frames that nothing is evicted.
// Also runs the single-level table to check that the hashes match.
// Plot B (hot pages) depends only on the trace, so it is the same as Part I's.
// Usage: part_ii [trace_dir] [max_accesses]
#include "benchmark.hpp"
#include "report_table.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv){
    std::string trace_dir = argc > 1 ? argv[1] : "traces";
    uint64_t max_accesses = argc > 2 ? strtoull(argv[2], nullptr, 10) : 0;

    MemoryConfig single_config, two_config;
    single_config.page_table = PageTableVariant::SINGLE;
    two_config.page_table = PageTableVariant::TWOLEVEL;

    ReportTable table("Part II: two-level page table, no TLB (single-level shown for comparison)",
        {{"benchmark", 12}, {"frames_used", 11}, {"util_2L_%", 10}, {"util_1L_%", 10},
         {"cycles_2L", 16}, {"cycles_1L", 16}, {"hash_2L", 16}, {"hash_match", 10}, {"seconds_2L", 10}});
    for(const std::string& trace : list_traces(trace_dir)){
        BenchmarkResult two = run_benchmark(trace, two_config, max_accesses);
        BenchmarkResult single = run_benchmark(trace, single_config, max_accesses);
        table.add_row({two.name, format("%d", two.frames_used),
                       format("%.4f", two.utilization * 100), format("%.4f", single.utilization * 100),
                       format("%llu", (unsigned long long)two.stats.cycles),
                       format("%llu", (unsigned long long)single.stats.cycles),
                       format("%016llx", (unsigned long long)two.stats.hash),
                       two.stats.hash == single.stats.hash ? "yes" : "NO", format("%.3f", two.seconds)});
    }
    table.write_csv("results/part_ii.csv");
}
