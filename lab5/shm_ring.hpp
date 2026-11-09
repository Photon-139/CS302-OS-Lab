#pragma once
#include "strip.hpp"
#include <semaphore.h>

struct shm_ring_control {
    sem_t empty; // Number of empty slots, starts as N
    sem_t full; // Number of filled slots, starts as 0
};

class shm_ring{
public:
    shm_ring(size_t capacity, int k, int width);
    ~shm_ring();

    shm_ring(const shm_ring&)            = delete;
    shm_ring& operator=(const shm_ring&) = delete;
    // Producer
    strip_view acquire();
    void       publish();

    // Consumer
    strip_view receive();
    void       release();   
private:
    size_t capacity_, slot_bytes_, region_bytes_;
    shm_ring_control* ctl_;
    strip_hdr* headers_;
    uint8_t* pixels_;
    uint64_t head_ = 0;
    uint64_t tail_ = 0;
};