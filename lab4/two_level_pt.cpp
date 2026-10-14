#include "two_level_pt.hpp"

TwoLevelPageTable::TwoLevelPageTable() : PageTable(2){}
int TwoLevelPageTable::page_to_frame(int vpn) const{
    const auto& pde = dir[vpn >> L2_BITS];
    if(!pde) return -1;
    const auto& pte = pde->entries[vpn & L2_MASK];
    return pte.is_valid ? pte.frame_number : -1;
}
void TwoLevelPageTable::add_mapping(int vpn, int frame_number){
    auto& pde = dir[vpn >> L2_BITS];
    if(!pde){
        pde = std::make_unique<L2Table>();
        l2_count++;
    }
    auto& pte = pde->entries[vpn & L2_MASK];
    if(!pte.is_valid){
        pde->valid_count++;
    }
    pte = {true, frame_number};
}
int TwoLevelPageTable::remove_mapping(int vpn) {
    auto& pde = dir[vpn >> L2_BITS];
    if(!pde) return -1;
    auto& pte = pde->entries[vpn & L2_MASK];
    if(!pte.is_valid) return -1;
    pte.is_valid = false;
    int frame = pte.frame_number;
    pde->valid_count--;
    if(!pde->valid_count){
        pde.reset();
        l2_count--;
    }
    return frame;
}
// Counts leaf PTEs only (the directory is fixed overhead and not counted).
int TwoLevelPageTable::entries_allocated() const {
    return l2_count * L2_SIZE;
}
