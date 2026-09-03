#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include "process.hpp"

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
                cpu_burst = false;
            }else{
                p.io_bursts.push_back(number);
                cpu_burst = true;
            }
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


    return 0;
}