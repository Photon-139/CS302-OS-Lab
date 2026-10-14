#pragma once
#include "page_table.hpp"
#include <array>
#include <memory>

struct L2Table{
    int valid_count = 0;
    std::array<PTE, L2_SIZE> entries;
};

class TwoLevelPageTable : public PageTable{
    std::array<std::unique_ptr<L2Table>, DIR_SIZE> dir;
    int l2_count = 0;   // number of live second-level tables
public:
    TwoLevelPageTable();
    int page_to_frame(int vpn) const override;
    void add_mapping(int vpn, int frame_number) override;
    int remove_mapping(int vpn) override;
    int entries_allocated() const override;
};