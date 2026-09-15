#pragma once

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
