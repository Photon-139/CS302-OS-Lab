#include "single_level_pt.hpp"

SingleLevelPageTable::SingleLevelPageTable() : PageTable(1), table(NUM_PAGES){}
int SingleLevelPageTable::page_to_frame(int vpn) const {
    return table[vpn].is_valid ? table[vpn].frame_number : -1;
}
void SingleLevelPageTable::add_mapping(int vpn, int frame_number)  {
    table[vpn].frame_number = frame_number;
    table[vpn].is_valid = true;
}
int SingleLevelPageTable::remove_mapping(int vpn) {
    if(!table[vpn].is_valid) return -1;
    table[vpn].is_valid = false;
    return table[vpn].frame_number;
}
int SingleLevelPageTable::entries_allocated() const {
    return NUM_PAGES;
}

