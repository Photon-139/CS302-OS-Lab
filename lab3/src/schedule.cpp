#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <algorithm>
#include <queue>
#include <memory>
#include <climits>
#include "process.hpp"

enum class EventType{ARRIVAL=0, IO_COMPLETION=1, CPU_RELEASE=2, PRIORITY_BOOST=3};

struct Event{
    int pid;
    int time;
    int epoch;
    int cpu_id;
    EventType event;


    Event(int p, int t, int e, int cid, EventType eve) : pid(p), time(t), epoch(e), cpu_id(cid), event(eve) {}

    bool operator<(const Event& other) const {
        if(other.time!=time){
            return time > other.time;
        }
        if(event!=other.event) {
            return event > other.event;
        }
        return pid > other.pid;
    }
};

struct ScheduleEntry{
    int cpu_id;
    int pid;
    int cpu_burst;
    int start_time;
    int end_time;
    ScheduleEntry(int cid, int p, int cb, int st, int et) : cpu_id(cid), pid(p), cpu_burst(cb), start_time(st), end_time(et) {}
};

class SchedulingPolicy {
public:
    virtual ~SchedulingPolicy() = default;
    virtual void ready_push(int pid) = 0;
    virtual void requeue(int pid, std::vector<Process>& processes) {};
    virtual std::pair<int, int> next_process(std::vector<Process>& processes) = 0;
    virtual bool has_ready() const = 0;
    virtual void priority_boost(std::vector<Process>& processes) {}
    virtual bool should_preempt(int running_pid, const std::vector<Process>& processes) const { return false; }
};

class FIFO : public SchedulingPolicy{
    std::queue<int> ready_queue;
public:
    void ready_push(int pid) override{
        ready_queue.push(pid);
    }
    std::pair<int, int> next_process(std::vector<Process>& processes) override{
        if(ready_queue.empty()){
            return {-1, -1};
        }
        int pid = ready_queue.front();
        ready_queue.pop();
        return {pid, processes[pid].cpu_bursts[processes[pid].cpu_burst_index]};
    }
    bool has_ready() const override {
        return !ready_queue.empty();
    }
};

class RoundRobin : public SchedulingPolicy{
    std::queue<int> ready_queue;
    int quantum;
public:
    explicit RoundRobin(int q) : quantum(q) {}

    void ready_push(int pid) override {
        ready_queue.push(pid);
    }
    void requeue(int pid, std::vector<Process>& processes) override {
        ready_queue.push(pid);
    }
    std::pair<int, int> next_process(std::vector<Process>& processes) override{
        if(ready_queue.empty()){
            return {-1, -1};
        }
        int pid = ready_queue.front();
        ready_queue.pop();
        return {pid, std::min(quantum, processes[pid].cpu_burst_remaining)};
    }
    bool has_ready() const override{
        return !ready_queue.empty();
    }
};

class MLFQ : public SchedulingPolicy{
    static constexpr int NUM_QUEUES = 3;
    static constexpr int QUANTUM = 2;
    std::queue<int> queues[NUM_QUEUES];
    int best_level() const {
        for(int l = 0; l<NUM_QUEUES; ++l){
            if(!queues[l].empty()){
                return l;
            }
        }
        return -1;
    }
    bool with_boost;
public:
    explicit MLFQ(bool with_boost) : with_boost(with_boost) {}
    int boost_period() const {
        return with_boost ? 20 : -1;
    }
    void ready_push(int pid) override {
        queues[0].push(pid);
    }
    void ready_push_at(int pid, int level){
        level = std::clamp(level, 0, NUM_QUEUES-1);
        queues[level].push(pid);
    }

    void requeue(int pid, std::vector<Process>& processes) override{
        int level = processes[pid].current_queue;
        queues[level].push(pid);
    }
    void requeue_at(int pid, int level){
        level = std::clamp(level, 0, NUM_QUEUES-1);
        queues[level].push(pid);
    }
    void demote(int pid, std::vector<Process>& processes){
        if(processes[pid].current_queue<NUM_QUEUES-1){
            processes[pid].current_queue++;
        }
        processes[pid].quantum_remaining = 0;
        requeue_at(pid, processes[pid].current_queue);
    }

    
    std::pair<int, int> next_process(std::vector<Process>& processes) override{
        int level = best_level();
        if(level==-1){
            return {-1, -1};
        }
        int pid = queues[level].front();
        queues[level].pop();
        int q = processes[pid].quantum_remaining > 0 ? processes[pid].quantum_remaining : QUANTUM;
        return {pid, std::min(q, processes[pid].cpu_burst_remaining)};
    }
    bool has_ready() const override{
        return best_level()!=-1;
    }
    void priority_boost(std::vector<Process>& processes) {
        if(!with_boost) return;
        for(int i = 1; i<NUM_QUEUES; ++i){
            while(!queues[i].empty()){
                int pid = queues[i].front();
                queues[i].pop();
                processes[pid].current_queue = 0;
                queues[0].push(pid);
            }
        }
        for(auto& p : processes) {
            p.current_queue = 0;
            p.quantum_remaining = QUANTUM;
        }
    }
    bool should_preempt(int running_pid, const std::vector<Process>& processes) const {
        int level = processes[running_pid].current_queue;
        int best = best_level();
        if(best==-1) return false;
        return best < level;
    }
    
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
void print_schedule(const std::vector<ScheduleEntry>& schedule, int num_cpus) {
    for (int cpu = 0; cpu < num_cpus; cpu++) {
        std::cout << "CPU" << cpu << "\n";
        for (const auto& e : schedule) {
            if (e.cpu_id != cpu) continue;
            std::cout << "P" << (e.pid+1) << "," << e.cpu_burst
                      << "\t" << (e.start_time+1) << "\t" << e.end_time << "\n";
        }
    }
}

void print_stats(const std::vector<Process>& processes, const std::vector<ScheduleEntry>& schedule, int num_cpus){
    int num_procs = processes.size();
    int total_turnaround = 0;
    int max_turnaround = 0;
    int min_turnaround = INT_MAX;
    
    int total_response = 0;
    int max_response = 0;
    int min_response = INT_MAX;

    std::vector<int> cpu_runtime(num_cpus, 0);
    for(const auto& e : schedule){
        cpu_runtime[e.cpu_id] += (e.end_time - e.start_time);
    }

    for(const auto& p : processes){
        int turnaround = p.completion_time - p.arrival_time;
        int response = p.first_service_time - p.arrival_time;
        max_turnaround = std::max(max_turnaround, turnaround);
        min_turnaround = std::min(min_turnaround, turnaround);
        max_response = std::max(max_response, response);
        min_response = std::min(min_response, response);
        total_turnaround += turnaround;
        total_response += response;
    }
    double avg_turnaround = (double)total_turnaround/num_procs;
    double avg_response = (double)total_response/num_procs;
    // std::cout << std::fixed << std::setprecision(2);
    std::cout << "Average turnaround time: " << avg_turnaround << "\n";
    std::cout << "Max turnaround time: " << max_turnaround << "\n";
    std::cout << "Min turnaround time: " << min_turnaround << "\n";
    std::cout << "Average response time: " << avg_response << "\n";
    std::cout << "Max response time: " << max_response << "\n";
    std::cout << "Min response time: " << min_response << "\n";
    
    std::cout << "CPU wise runtime" << std::endl;
    for(int cpu = 0; cpu<num_cpus; ++cpu){
        std::cout << "CPU" << cpu << ": " << cpu_runtime[cpu] << std::endl;
    }

}

int main(int argc, char* argv[]){
    if(argc!=4){
        std::cout << "Usage: schedule.out <algorithm> <path-to-workload> <cpu-count>" << std::endl;
        exit(1);
    }
    std::string algorithm(argv[1]);
    const char* file_path = argv[2];
    int num_cpus = std::stoi(argv[3]);
    std::vector<Process> processes = parse_workload(file_path);
    
    int processed = 0;
    std::priority_queue<Event> event_queue;
    for(const auto& p : processes){
        event_queue.emplace(p.pid, p.arrival_time, 0, -1, EventType::ARRIVAL);
    }
    int num_processes = processes.size();
    bool is_mlfq = (algorithm == "MLFQ" || algorithm == "MLFQ-BOOST");
    std::unique_ptr<SchedulingPolicy> policy_base;

    if (algorithm == "FIFO") {
        policy_base = std::make_unique<FIFO>();
    } else if (algorithm.rfind("RR-", 0) == 0) {
        int q = std::stoi(algorithm.substr(3));
        policy_base = std::make_unique<RoundRobin>(q);
    } else if (algorithm == "MLFQ") {
        policy_base = std::make_unique<MLFQ>(false);
    } else if (algorithm == "MLFQ-BOOST") {
        policy_base = std::make_unique<MLFQ>(true);
    } else {
        std::cerr << "Unknown algorithm: " << algorithm << "\n";
        exit(1);
    }

    MLFQ* mlfq = is_mlfq ? static_cast<MLFQ*>(policy_base.get()) : nullptr;
    std::vector<int> cpu_process(num_cpus, -1);  // pid on each cpu; -1 = idle
    
    if(is_mlfq && mlfq->boost_period()!=-1){
        event_queue.emplace(-1, mlfq->boost_period(), 0, -1, EventType::PRIORITY_BOOST);
    }

    auto do_dispatch = [&](int current_time){
        for(int cpu_id = 0; cpu_id<num_cpus; cpu_id++){
            if(cpu_process[cpu_id]!=-1) continue;
            if(!policy_base->has_ready()) break;
            auto [pid, burst] = policy_base->next_process(processes);
            cpu_process[cpu_id] = pid;
            processes[pid].cpu_start_time = current_time;

            if(is_mlfq){
                processes[pid].quantum_remaining = burst;
            }

            if(processes[pid].first_service_time==-1){
                processes[pid].first_service_time = current_time;
            }

            event_queue.emplace(pid, current_time + burst, processes[pid].epoch, cpu_id, EventType::CPU_RELEASE);
        }
    };
    std::vector<ScheduleEntry> schedule;

    auto preempt_cpu = [&](int cpu, int current_time) {
        int pid = cpu_process[cpu];
        int elapsed = current_time - processes[pid].cpu_start_time;
        if (elapsed == 0) return;
        if(is_mlfq){
            processes[pid].quantum_remaining -= elapsed;
        }
        processes[pid].cpu_burst_remaining -= elapsed;
        
        schedule.emplace_back(cpu, pid, processes[pid].cpu_burst_index+1, processes[pid].cpu_start_time, current_time);

        processes[pid].epoch++;
        cpu_process[cpu] = -1;
        policy_base->requeue(pid, processes);
    };
    

    int current_time = 0;

    auto preempt_lowest_if_needed = [&](){
        if(!is_mlfq) return;

        int victim_cpu  = -1;
        int worst_level = -1;
        for (int cpu = 0; cpu < num_cpus; cpu++) {
            if (cpu_process[cpu] == -1) continue;
            int lv = processes[cpu_process[cpu]].current_queue;
            if (lv > worst_level) { 
                worst_level = lv;
                victim_cpu = cpu;
            }
        }
        if (victim_cpu != -1 && mlfq->should_preempt(cpu_process[victim_cpu], processes)) {
            preempt_cpu(victim_cpu, current_time);
        }
    };
    while(processed<num_processes){
        Event e = event_queue.top(); event_queue.pop();
        if(e.time > current_time){
            current_time = e.time;
        }
        
        if(e.event==EventType::ARRIVAL){
            policy_base->ready_push(e.pid);

            if (is_mlfq) {
                preempt_lowest_if_needed();
            }
        }else if(e.event==EventType::CPU_RELEASE){
            if(e.epoch != processes[e.pid].epoch) continue;

            int pid = e.pid;
            int cpu = e.cpu_id;

            int elapsed = current_time - processes[pid].cpu_start_time;
            processes[pid].cpu_burst_remaining-= elapsed;

            schedule.emplace_back(cpu, pid, processes[pid].cpu_burst_index+1, processes[pid].cpu_start_time, current_time);

            cpu_process[cpu] = -1;

            if(processes[pid].cpu_burst_remaining>0){
                if(is_mlfq){
                    mlfq->demote(pid, processes);
                }else{
                    policy_base->requeue(pid, processes);
                }
            }else{
                processes[pid].quantum_remaining = 0;
                int next_cpu_idx = processes[pid].cpu_burst_index + 1;
                if(next_cpu_idx < processes[pid].cpu_bursts.size()){
                    int io_dur = processes[pid].io_bursts[processes[pid].io_burst_index++];
                    processes[pid].cpu_burst_index = next_cpu_idx;
                    processes[pid].cpu_burst_remaining = processes[pid].cpu_bursts[next_cpu_idx];
                    event_queue.emplace(pid, current_time + io_dur, processes[pid].epoch, -1, EventType::IO_COMPLETION);
                }else{
                    processes[pid].completion_time = current_time;
                    processed++;
                }
            }
        }else if(e.event == EventType::IO_COMPLETION){
            int pid = e.pid;
            if (is_mlfq){
                mlfq->ready_push_at(pid, processes[pid].current_queue);
                preempt_lowest_if_needed();
            }else{
                policy_base->ready_push(pid);
            }
        }else if(e.event==EventType::PRIORITY_BOOST){
            for(int cpu = 0; cpu<num_cpus; ++cpu){
                int pid = cpu_process[cpu];
                if(pid!=-1 && processes[pid].current_queue>0){
                    preempt_cpu(cpu, current_time);
                }
            }
            policy_base->priority_boost(processes);
            if(mlfq->boost_period()!=-1){
                event_queue.emplace(-1, e.time + mlfq->boost_period(), 0, -1, EventType::PRIORITY_BOOST);
            }
        }
        do_dispatch(current_time);
    }
    std::cout << algorithm << " " << file_path << " " << num_cpus << std::endl;

    print_schedule(schedule, num_cpus);
    print_stats(processes, schedule, num_cpus);
    std::cout << "================================" << std::endl;
    return 0;
}