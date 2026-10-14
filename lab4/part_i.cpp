// Part I: single-level page table, no TLB, enough frames that nothing is evicted.
// Usage: part_i [trace_dir] [max_accesses]
#include "benchmark.hpp"
#include "report_table.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>

// Writes the hot-pages curve (stat B): touched pages sorted hottest first, and the
// fraction of all accesses that the hottest x% of them account for.
// Returns the access share of the hottest 10% of touched pages.
static double write_hot_pages(const std::string& name, const std::vector<uint32_t>& page_counts){
    std::vector<uint32_t> counts;
    for(uint32_t c : page_counts){
        if(c > 0) counts.push_back(c);
    }
    std::sort(counts.begin(), counts.end(), std::greater<uint32_t>());

    uint64_t total = 0;
    for(uint32_t c : counts) total += c;

    std::string file = "results/hot_pages_" + name + ".csv";
    std::filesystem::create_directories("results");
    FILE* out = fopen(file.c_str(), "w");
    if(!out){
        fprintf(stderr, "Could not write %s\n", file.c_str());
        exit(1);
    }
    fprintf(out, "page_fraction,access_fraction\n");
    uint64_t running = 0;
    double top10 = 0.0;
    size_t step = std::max<size_t>(1, counts.size() / 1000);    // sample so the file stays small
    for(size_t i = 0; i < counts.size(); ++i){
        running += counts[i];
        double access_fraction = static_cast<double>(running) / total;
        if(i + 1 == std::max<size_t>(1, counts.size() / 10)) top10 = access_fraction;
        if(i % step == 0 || i + 1 == counts.size()){
            fprintf(out, "%.6f,%.6f\n", static_cast<double>(i + 1) / counts.size(), access_fraction);
        }
    }
    fclose(out);
    return top10;
}

int main(int argc, char** argv){
    std::string trace_dir = argc > 1 ? argv[1] : "traces";
    uint64_t max_accesses = argc > 2 ? strtoull(argv[2], nullptr, 10) : 0;

    MemoryConfig config;
    config.page_table = PageTableVariant::SINGLE;

    ReportTable table("Part I: single-level page table, no TLB",
        {{"benchmark", 12}, {"accesses", 10}, {"frames_used", 11}, {"pt_util_%", 10},
         {"top10%_share_%", 14}, {"cycles", 16}, {"hash", 16}, {"seconds", 8}});
    for(const std::string& trace : list_traces(trace_dir)){
        std::vector<uint32_t> page_counts;
        BenchmarkResult r = run_benchmark(trace, config, max_accesses, &page_counts);
        double top10 = write_hot_pages(r.name, page_counts);
        table.add_row({r.name, format("%llu", (unsigned long long)r.stats.accesses), format("%d", r.frames_used),
                       format("%.4f", r.utilization * 100), format("%.2f", top10 * 100),
                       format("%llu", (unsigned long long)r.stats.cycles),
                       format("%016llx", (unsigned long long)r.stats.hash), format("%.3f", r.seconds)});
    }
    printf("\ntop10%%_share_%% = share of all accesses that go to the hottest 10%% of the touched pages\n");
    table.write_csv("results/part_i.csv");
}
