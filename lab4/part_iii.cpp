// Part III: add a 128-entry TLB to both the single-level and the two-level simulators.
// seconds_two (two-level with TLB, the full simulator) is the Part V timing.
// Prints one CSV row per benchmark. Usage: part_iii [trace_dir] [max_accesses]
#include "benchmark.hpp"
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

    printf("benchmark,accesses,tlb_hit_rate_single,tlb_hit_rate_two,cycles_single,cycles_two,"
           "hash_match,seconds_single,seconds_two\n");
    for(const std::string& trace : list_traces(trace_dir)){
        BenchmarkResult single = run_benchmark(trace, single_config, max_accesses);
        BenchmarkResult two = run_benchmark(trace, two_config, max_accesses);
        printf("%s,%llu,%.6f,%.6f,%llu,%llu,%s,%.3f,%.3f\n", single.name.c_str(),
               (unsigned long long)single.stats.accesses,
               tlb_hit_rate(single.stats), tlb_hit_rate(two.stats),
               (unsigned long long)single.stats.cycles, (unsigned long long)two.stats.cycles,
               single.stats.hash == two.stats.hash ? "yes" : "NO", single.seconds, two.seconds);
    }
}
