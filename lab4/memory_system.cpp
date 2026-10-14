#include "memory_system.hpp"
#include <cstdlib>
#include <iostream>

MemorySystem::MemorySystem(const MemoryConfig& config) : vpn_accesses(NUM_PAGES, 0){
    if(config.num_frames <= 0 || config.num_frames > NUM_PAGES){
        std::cerr << "Frame count must be between 1 and " << NUM_PAGES << std::endl;
        exit(1);
    }

    if(config.page_table == PageTableVariant::SINGLE){
        page_table = std::make_unique<SingleLevelPageTable>();
    }else{
        page_table = std::make_unique<TwoLevelPageTable>();
    }

    // Enough frames for every page means nothing is ever evicted.
    if(config.num_frames == NUM_PAGES){
        frame_manager = std::make_unique<BumpFrameManager>(config.num_frames);
    }else{
        frame_manager = std::make_unique<LRUFrameManager>(config.num_frames);
    }

    if(config.use_tlb){
        tlb = std::make_unique<TLB>(TLB_ENTRIES);
    }
}

void MemorySystem::memory_access(uint32_t address){
    int vpn = address >> OFFSET_BITS;
    stats.accesses++;
    vpn_accesses[vpn]++;

    int frame = -1;
    if(tlb){
        stats.cycles += TLB_COST;
        frame = tlb->get_frame(vpn);
        if(frame != -1){
            stats.tlb_hits++;
        }
    }

    if(frame == -1){
        // TLB miss (or no TLB): walk the page table.
        stats.cycles += static_cast<uint64_t>(page_table->walk_accesses) * MEM_COST;
        frame = page_table->page_to_frame(vpn);
        if(frame == -1){
            frame = handle_page_fault(vpn);
        }
        if(tlb){
            tlb->insert(vpn, frame);
        }
    }

    // The actual data access.
    stats.cycles += MEM_COST;
    frame_manager->on_access(frame);
    stats.hash = (stats.hash ^ static_cast<uint64_t>(frame)) * 1099511628211ULL;
}

// Brings vpn into memory, evicting the LRU page if no frame is free. Returns its frame.
int MemorySystem::handle_page_fault(int vpn){
    stats.page_faults++;
    if(!frame_manager->has_free()){
        int victim = frame_manager->evict_page();
        page_table->remove_mapping(victim);
        if(tlb){
            tlb->invalidate_entry(victim);
        }
        stats.evictions++;
        stats.cycles += DISK_COST;
    }
    int frame = frame_manager->allocate(vpn);
    page_table->add_mapping(vpn, frame);
    return frame;
}

const MemoryStats& MemorySystem::get_stats() const{
    return stats;
}

int MemorySystem::frames_used() const{
    return frame_manager->frames_used();
}

double MemorySystem::page_table_utilization() const{
    int allocated = page_table->entries_allocated();
    if(allocated == 0) return 0.0;
    int touched = 0;
    for(uint32_t count : vpn_accesses){
        if(count > 0) touched++;
    }
    return static_cast<double>(touched) / allocated;
}

const std::vector<uint32_t>& MemorySystem::page_access_counts() const{
    return vpn_accesses;
}
