#pragma once
#include "frame_manager.hpp"

// For runs where physical memory is big enough that nothing is ever evicted
// (Parts I-III). Frames are handed out in order 0, 1, 2, ... and never reused.
class BumpFrameManager : public FrameManager{
    int num_frames;
    int next = 0;       // next frame to hand out
public:
    explicit BumpFrameManager(int num_frames);

    bool has_free() const override;
    int allocate(int vpn) override;
    int evict_page() override;      // never valid for this manager
    void on_access(int frame) override;
    int frames_used() const override;
};
