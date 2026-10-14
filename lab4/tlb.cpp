#include "tlb.hpp"
#include <limits>

TLB::TLB(int capacity) : entries(capacity) {}
int TLB::get_frame(int vpn){
    counter++;
    for(auto& entry : entries){
        if(entry.is_valid && entry.vpn==vpn){
            entry.last_used = counter;
            return entry.pfn;
        }
    }
    return -1;
}

void TLB::invalidate_entry(int vpn){
    for(auto& entry : entries){
        if(entry.is_valid && entry.vpn==vpn){
            entry.is_valid = false;
            break;
        }
    }
}

void TLB::insert(int vpn, int frame){
    // Assuming the vpn is not already present in the cache
    counter++;
    int candidate = -1;
    long long lru_time = std::numeric_limits<long long>::max();
    for(size_t i = 0; i<entries.size(); ++i){
        if(!entries[i].is_valid){
            candidate = i;
            break;
        }
        if(lru_time>entries[i].last_used){
            lru_time = entries[i].last_used;
            candidate = i;
        }
    }
    entries[candidate].is_valid = true;
    entries[candidate].pfn = frame;
    entries[candidate].vpn = vpn;
    entries[candidate].last_used = counter;
}