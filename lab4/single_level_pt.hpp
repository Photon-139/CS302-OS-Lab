#pragma once
#include "page_table.hpp"
#include <vector>

class SingleLevelPageTable: public PageTable{
    std::vector<PTE> table;
public:
    SingleLevelPageTable();
    int page_to_frame(int vpn) const override;
    void add_mapping(int vpn, int frame_number) override;
    int remove_mapping(int vpn) override;
    int entries_allocated() const override;
};

