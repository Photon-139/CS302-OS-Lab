#pragma once

// Address geometry: 32-bit VA, 4 KB pages -> 20-bit VPN, split 10/10 for two levels.
constexpr int OFFSET_BITS = 12;
constexpr int VPN_BITS = 32 - OFFSET_BITS;
constexpr int NUM_PAGES = 1 << VPN_BITS;
constexpr int L2_BITS = 10;
constexpr int L2_SIZE = 1 << L2_BITS;
constexpr int L2_MASK = L2_SIZE - 1;
constexpr int DIR_SIZE = 1 << (VPN_BITS - L2_BITS);

struct PTE{
  bool is_valid = false;
  int frame_number = -1;
};

class PageTable {
public:
  virtual int page_to_frame(int vpn) const = 0;
  virtual void add_mapping(int vpn, int frame_number) = 0;
  virtual int remove_mapping(int vpn) = 0;
  virtual ~PageTable() = default;
  const int walk_accesses;
  virtual int entries_allocated() const = 0;
protected:
  explicit PageTable(int walk) : walk_accesses(walk){};
};