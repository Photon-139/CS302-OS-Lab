#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "memory_system.hpp"

// Result of simulating one trace under one configuration.
struct BenchmarkResult{
    std::string name;
    MemoryStats stats;
    int frames_used = 0;
    double utilization = 0.0;
    double seconds = 0.0;       // wall-clock time to build the simulator, read the trace and simulate it
};

// Trace files in `dir` ("*_memoryTrace.txt"), sorted by name.
std::vector<std::string> list_traces(const std::string& dir);

// Runs a trace through a fresh MemorySystem. max_accesses = 0 means the whole trace.
// If page_counts is given, it receives the per-page access counts.
BenchmarkResult run_benchmark(const std::string& path, const MemoryConfig& config,
                              uint64_t max_accesses = 0, std::vector<uint32_t>* page_counts = nullptr);

// Fraction of accesses that hit in the TLB (the TLB is looked up on every access).
double tlb_hit_rate(const MemoryStats& stats);
