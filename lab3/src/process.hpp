#pragma once

#include <vector>

struct Process{
    int pid;
    int arrival_time;
    std::vector<int> cpu_bursts;
    std::vector<int> io_bursts;
    int remaining_time;
    int completion_time = -1;
    int first_service_time = -1;
};