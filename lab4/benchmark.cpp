#include "benchmark.hpp"
#include "trace_reader.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static const std::string TRACE_SUFFIX = "_memoryTrace.txt";

// "traces/gcc_memoryTrace.txt" -> "gcc"
static std::string benchmark_name(const std::string& path){
    std::string file = fs::path(path).filename().string();
    return file.substr(0, file.size() - TRACE_SUFFIX.size());
}

std::vector<std::string> list_traces(const std::string& dir){
    std::vector<std::string> traces;
    if(!fs::is_directory(dir)){
        std::cerr << "Trace directory not found: " << dir << std::endl;
        exit(1);
    }
    for(const auto& entry : fs::directory_iterator(dir)){
        std::string file = entry.path().filename().string();
        if(entry.is_regular_file() && file.size() > TRACE_SUFFIX.size() &&
           file.compare(file.size() - TRACE_SUFFIX.size(), TRACE_SUFFIX.size(), TRACE_SUFFIX) == 0){
            traces.push_back(entry.path().string());
        }
    }
    std::sort(traces.begin(), traces.end());
    return traces;
}

BenchmarkResult run_benchmark(const std::string& path, const MemoryConfig& config,
                              uint64_t max_accesses, std::vector<uint32_t>* page_counts){
    BenchmarkResult result;
    result.name = benchmark_name(path);

    auto start = std::chrono::steady_clock::now();
    MemorySystem memory(config);
    TraceReader reader(path.c_str());
    uint32_t address;
    uint64_t count = 0;
    while((max_accesses == 0 || count < max_accesses) && reader.next(address)){
        memory.memory_access(address);
        count++;
    }
    auto end = std::chrono::steady_clock::now();

    result.seconds = std::chrono::duration<double>(end - start).count();
    result.stats = memory.get_stats();
    result.frames_used = memory.frames_used();
    result.utilization = memory.page_table_utilization();
    if(page_counts){
        *page_counts = memory.page_access_counts();
    }
    return result;
}

double tlb_hit_rate(const MemoryStats& stats){
    return stats.accesses == 0 ? 0.0 : static_cast<double>(stats.tlb_hits) / stats.accesses;
}
