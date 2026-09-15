#pragma once
#include "process.hpp"
#include <queue>
#include <algorithm>

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
    void ready_push(int pid) override;
    std::pair<int, int> next_process(std::vector<Process>& processes) override;
    bool has_ready() const override;
};

class RoundRobin : public SchedulingPolicy{
    std::queue<int> ready_queue;
    int quantum;
public:
    explicit RoundRobin(int q);
    void ready_push(int pid) override;
    void requeue(int pid, std::vector<Process>& processes) override;
    std::pair<int, int> next_process(std::vector<Process>& processes) override;
    bool has_ready() const override;
};

class MLFQ : public SchedulingPolicy{
    static constexpr int NUM_QUEUES = 3;
    static constexpr int QUANTUM = 2;
    std::queue<int> queues[NUM_QUEUES];
    int best_level() const;
    bool with_boost;
public:
    explicit MLFQ(bool with_boost);
    int boost_period() const;
    void ready_push(int pid) override;
    void ready_push_at(int pid, int level);
    void requeue(int pid, std::vector<Process>& processes) override;
    void requeue_at(int pid, int level);
    void demote(int pid, std::vector<Process>& processes);
    std::pair<int, int> next_process(std::vector<Process>& processes) override;
    bool has_ready() const override;
    void priority_boost(std::vector<Process>& processes);
    bool should_preempt(int running_pid, const std::vector<Process>& processes) const;
};
