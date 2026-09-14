#pragma once

#include <vector>

struct Process{
    int pid;
    int arrival_time;
    std::vector<int> cpu_bursts;
    std::vector<int> io_bursts;

    int io_burst_index = -1;
    int cpu_burst_index = -1;
    int cpu_burst_remaining;

    int epoch = 0;
    int quantum_remaining = 0;
    int cpu_start_time = -1;

    int current_queue = 0;
    
    int first_service_time = -1;
    int completion_time = -1;
};