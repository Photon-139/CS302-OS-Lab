#pragma once
#include "frame_manager.hpp"
#include <vector>

// Limited physical memory with LRU replacement (Part IV).
class LRUFrameManager : public FrameManager{
    struct FrameNode{
        int next, prev, vpn;
    };
    // Indexed by frame. Two extra nodes at the end are the list sentinels:
    // frame_nodes[head_s].next is the MRU frame, frame_nodes[tail_s].prev the LRU frame.
    // The list is empty when frame_nodes[head_s].next == tail_s.
    std::vector<FrameNode> frame_nodes;
    int num_frames;
    int head_s, tail_s;
    int next_unused = 0;            // frames [0, next_unused) have been handed out at least once
    // Frame released by evict_page() and not yet reused, or -1. One slot is enough
    // because every eviction is immediately followed by allocate(). If frames can
    // ever be released without a matching allocate, this must become a free list.
    int reclaimed_frame = -1;

    void unlink(int frame);         // detach frame from the list
    void push_front(int frame);     // insert frame right after the head sentinel (MRU)

public:
    explicit LRUFrameManager(int num_frames);

    bool has_free() const override;
    int allocate(int vpn) override;     // new page becomes the most recently used
    int evict_page() override;          // evicts the least recently used page
    void on_access(int frame) override; // moves frame to most recently used
    int frames_used() const override;
};
