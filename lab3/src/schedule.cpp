#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <algorithm>
#include <queue>
#include "process.hpp"

enum class EventType{ARRIVAL=0, IO_COMPLETION=1, CPU_RELEASE=2, PRIORITY_BOOST=3};

struct Event{
    int pid;
    int time;
    int epoch;
    int cpu_id;
    EventType event;

    bool operator<(const Event& other) const {
        if(other.time!=time){
            return time > other.time;
        }
        return event > other.event;
    }
};

struct ScheduleEntry{
    int cpu_id;
    int pid;
    int cpu_burst;
    int start_time;
    int end_time;
};

class SchedulingPolicy{
public:
    virtual void ready_push(int pid) = 0;
    virtual void requeue(int pid) = 0;


};

std::vector<Process> parse_workload(const char* file_path){
    std::ifstream inputFile(file_path);
    if(!inputFile.is_open()){
        std::cout << "Error opening file at path " << file_path << std::endl;
        exit(1);
    }
    std::vector<Process> processes;
    int curr_pid = 0;
    std::string line;
    while(std::getline(inputFile, line)){
        if(line.empty()) continue;

        std::istringstream line_stream(line);
        Process p;
        line_stream >> p.arrival_time;
        p.pid = curr_pid++;

        int number;
        bool cpu_burst = true;
        while(line_stream >> number){
            if(number==-1) break;
            if(cpu_burst){
                p.cpu_bursts.push_back(number);
            }else{
                p.io_bursts.push_back(number);
            }
            cpu_burst = !cpu_burst;
        }
        if(p.cpu_bursts.empty()){
            std::cerr << "No burst timings provided, invalid process" << std::endl;
            exit(1);
        }
        p.cpu_burst_remaining = p.cpu_bursts[0];
        p.cpu_burst_index = 0;
        if(!p.io_bursts.empty()){
            p.io_burst_index = 0;
        }
        processes.push_back(std::move(p));
    }
    inputFile.close();
    return processes;
}


int main(int argc, char* argv[]){
    if(argc!=3){
        std::cout << "Usage: schedule.out <algorithm> <path-to-workload>" << std::endl;
        exit(1);
    }
    std::string algorithm(argv[1]);
    const char* file_path = argv[2];

    std::cout << "Algorithm: " << algorithm << std::endl;
    std::cout << "Workload path: " << file_path << std::endl;

    std::vector<Process> processes = parse_workload(file_path);
    std::sort(processes.begin(), processes.end(), [](const Process& p1, const Process& p2){
        return (p1.arrival_time < p2.arrival_time) || (p1.arrival_time==p2.arrival_time && p1.pid < p2.pid); 
    });


    return 0;
}