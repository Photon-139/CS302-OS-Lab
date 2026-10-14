#pragma once
#include <vector>

class TLB{
    struct TLB_Entry{
        int vpn = -1;
        int pfn = -1;
        bool is_valid = false;
        long long last_used = -1;
    };
    std::vector<TLB_Entry> entries;
    long long counter = 0;
public:
    explicit TLB(int capacity = 128);
    int get_frame(int vpn);
    void invalidate_entry(int vpn);
    void insert(int vpn, int frame);
};