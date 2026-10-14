#include "bump_frame_manager.hpp"
#include <cassert>

BumpFrameManager::BumpFrameManager(int num_frames) : num_frames(num_frames){}

bool BumpFrameManager::has_free() const{
    return next < num_frames;
}

int BumpFrameManager::allocate(int /*vpn*/){
    assert(has_free());
    return next++;
}

int BumpFrameManager::evict_page(){
    assert(false && "BumpFrameManager cannot evict; give it enough frames");
    return -1;
}

void BumpFrameManager::on_access(int /*frame*/){
    // Nothing to track without a replacement policy.
}

int BumpFrameManager::frames_used() const{
    return next;
}
