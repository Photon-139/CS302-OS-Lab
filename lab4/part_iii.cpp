// Part III: add a 128-entry TLB to both the single-level and the two-level simulators.
// seconds_2L (two-level with TLB, the full simulator) is the Part V timing.
// Usage: part_iii [trace_dir] [max_accesses]
#include "benchmark.hpp"
#include "report_table.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv){
    std::string trace_dir = argc > 1 ? argv[1] : "traces";
    uint64_t max_accesses = argc > 2 ? strtoull(argv[2], nullptr, 10) : 0;

    MemoryConfig single_config, two_config;
    single_config.page_table = PageTableVariant::SINGLE;
    single_config.use_tlb = true;
    two_config.page_table = PageTableVariant::TWOLEVEL;
    two_config.use_tlb = true;

    ReportTable table("Part III: 128-entry TLB, single-level (1L) and two-level (2L) page tables",
        {{"benchmark", 12}, {"accesses", 10}, {"tlb_hit_1L_%", 12}, {"tlb_hit_2L_%", 12},
         {"cycles_1L", 16}, {"cycles_2L", 16}, {"hash_match", 10}, {"seconds_1L", 10}, {"seconds_2L", 10}});
    for(const std::string& trace : list_traces(trace_dir)){
        BenchmarkResult single = run_benchmark(trace, single_config, max_accesses);
        BenchmarkResult two = run_benchmark(trace, two_config, max_accesses);
        table.add_row({single.name, format("%llu", (unsigned long long)single.stats.accesses),
                       format("%.4f", tlb_hit_rate(single.stats) * 100), format("%.4f", tlb_hit_rate(two.stats) * 100),
                       format("%llu", (unsigned long long)single.stats.cycles),
                       format("%llu", (unsigned long long)two.stats.cycles),
                       single.stats.hash == two.stats.hash ? "yes" : "NO",
                       format("%.3f", single.seconds), format("%.3f", two.seconds)});
    }
    table.write_csv("results/part_iii.csv");
}
