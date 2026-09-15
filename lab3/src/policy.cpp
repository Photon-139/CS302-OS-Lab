#include "policy.hpp"

void FIFO::ready_push(int pid){
    ready_queue.push(pid);
}
std::pair<int, int> FIFO::next_process(std::vector<Process>& processes) {
    if(ready_queue.empty()){
        return {-1, -1};
    }
    int pid = ready_queue.front();
    ready_queue.pop();
    return {pid, processes[pid].cpu_bursts[processes[pid].cpu_burst_index]};
}
bool FIFO::has_ready() const {
    return !ready_queue.empty();
}

RoundRobin::RoundRobin(int q) : quantum(q) {}

void RoundRobin::ready_push(int pid) {
    ready_queue.push(pid);
}
void RoundRobin::requeue(int pid, std::vector<Process>& processes) {
    ready_queue.push(pid);
}
std::pair<int, int> RoundRobin::next_process(std::vector<Process>& processes) {
    if(ready_queue.empty()){
        return {-1, -1};
    }
    int pid = ready_queue.front();
    ready_queue.pop();
    return {pid, std::min(quantum, processes[pid].cpu_burst_remaining)};
}
bool RoundRobin::has_ready() const {
    return !ready_queue.empty();
}

int MLFQ::best_level() const {
    for(int l = 0; l<NUM_QUEUES; ++l){
        if(!queues[l].empty()){
            return l;
        }
    }
    return -1;
}
MLFQ::MLFQ(bool with_boost) : with_boost(with_boost) {}
int MLFQ::boost_period() const {
    return with_boost ? 20 : -1;
}
void MLFQ::ready_push(int pid) {
    queues[0].push(pid);
}
void MLFQ::ready_push_at(int pid, int level){
    level = std::clamp(level, 0, NUM_QUEUES-1);
    queues[level].push(pid);
}

void MLFQ::requeue(int pid, std::vector<Process>& processes) {
    int level = processes[pid].current_queue;
    queues[level].push(pid);
}
void MLFQ::requeue_at(int pid, int level){
    level = std::clamp(level, 0, NUM_QUEUES-1);
    queues[level].push(pid);
}
void MLFQ::demote(int pid, std::vector<Process>& processes){
    if(processes[pid].current_queue<NUM_QUEUES-1){
        processes[pid].current_queue++;
    }
    processes[pid].quantum_remaining = 0;
    requeue_at(pid, processes[pid].current_queue);
}


std::pair<int, int> MLFQ::next_process(std::vector<Process>& processes){
    int level = best_level();
    if(level==-1){
        return {-1, -1};
    }
    int pid = queues[level].front();
    queues[level].pop();
    int q = processes[pid].quantum_remaining > 0 ? processes[pid].quantum_remaining : QUANTUM;
    return {pid, std::min(q, processes[pid].cpu_burst_remaining)};
}
bool MLFQ::has_ready() const {
    return best_level()!=-1;
}
void MLFQ::priority_boost(std::vector<Process>& processes) {
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
bool MLFQ::should_preempt(int running_pid, const std::vector<Process>& processes) const {
    int level = processes[running_pid].current_queue;
    int best = best_level();
    if(best==-1) return false;
    return best < level;
}