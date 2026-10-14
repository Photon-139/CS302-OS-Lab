// Part II: two-level page table, no TLB, enough frames that nothing is evicted.
// Also runs the single-level table to check that the hashes match.
// Plot B (hot pages) depends only on the trace, so it is the same as Part I's.
// Prints one CSV row per benchmark. Usage: part_ii [trace_dir] [max_accesses]
#include "benchmark.hpp"
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv){
    std::string trace_dir = argc > 1 ? argv[1] : "traces";
    uint64_t max_accesses = argc > 2 ? strtoull(argv[2], nullptr, 10) : 0;

    MemoryConfig single_config, two_config;
    single_config.page_table = PageTableVariant::SINGLE;
    two_config.page_table = PageTableVariant::TWOLEVEL;

    printf("benchmark,accesses,frames_used,pt_utilization_two,pt_utilization_single,"
           "cycles_two,cycles_single,hash_two,hash_single,hash_match,seconds_two\n");
    for(const std::string& trace : list_traces(trace_dir)){
        BenchmarkResult two = run_benchmark(trace, two_config, max_accesses);
        BenchmarkResult single = run_benchmark(trace, single_config, max_accesses);
        printf("%s,%llu,%d,%.8f,%.8f,%llu,%llu,%llx,%llx,%s,%.3f\n", two.name.c_str(),
               (unsigned long long)two.stats.accesses, two.frames_used,
               two.utilization, single.utilization,
               (unsigned long long)two.stats.cycles, (unsigned long long)single.stats.cycles,
               (unsigned long long)two.stats.hash, (unsigned long long)single.stats.hash,
               two.stats.hash == single.stats.hash ? "yes" : "NO", two.seconds);
    }
}
