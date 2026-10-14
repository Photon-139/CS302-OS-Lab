#include "lru_frame_manager.hpp"
#include <cassert>

LRUFrameManager::LRUFrameManager(int num_frames)
    : frame_nodes(num_frames + 2), num_frames(num_frames),
      head_s(num_frames), tail_s(num_frames + 1){
    // Empty list: head <-> tail.
    frame_nodes[head_s].next = tail_s;
    frame_nodes[tail_s].prev = head_s;
}

void LRUFrameManager::unlink(int frame){
    frame_nodes[frame_nodes[frame].prev].next = frame_nodes[frame].next;
    frame_nodes[frame_nodes[frame].next].prev = frame_nodes[frame].prev;
}

void LRUFrameManager::push_front(int frame){
    int first = frame_nodes[head_s].next;
    frame_nodes[frame].prev = head_s;
    frame_nodes[frame].next = first;
    frame_nodes[first].prev = frame;
    frame_nodes[head_s].next = frame;
}

bool LRUFrameManager::has_free() const{
    return reclaimed_frame != -1 || next_unused < num_frames;
}

int LRUFrameManager::allocate(int vpn){
    assert(has_free());
    int frame;
    if(reclaimed_frame != -1){
        frame = reclaimed_frame;
        reclaimed_frame = -1;
    }else{
        frame = next_unused++;
    }
    frame_nodes[frame].vpn = vpn;
    push_front(frame);
    return frame;
}

int LRUFrameManager::evict_page(){
    int victim = frame_nodes[tail_s].prev;
    assert(victim != head_s && "nothing to evict");
    unlink(victim);
    reclaimed_frame = victim;
    return frame_nodes[victim].vpn;
}

void LRUFrameManager::on_access(int frame){
    if(frame_nodes[head_s].next == frame) return;   // already the most recently used
    unlink(frame);
    push_front(frame);
}

int LRUFrameManager::frames_used() const{
    return next_unused;
}
