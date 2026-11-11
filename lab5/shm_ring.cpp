#include "shm_ring.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

shm_ring::shm_ring(size_t capacity, int k, int width):
capacity_(capacity),
slot_bytes_((size_t)k*width*3),
width_(width)
{
    region_bytes_ = sizeof(shm_ring_control) + capacity * sizeof(strip_hdr) + capacity * slot_bytes_;
    static int counter = 0;
    std::string name = "/lab5_" + std::to_string(getpid()) + "_" + std::to_string(counter++);

    int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if(fd<0){
        perror("shm_open");
        abort();
    }

    if(ftruncate(fd, (off_t)region_bytes_)<0){
        perror("ftruncate");
        abort();
    }

    void* base = mmap(nullptr, region_bytes_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(base==MAP_FAILED){
        perror("mmap");
        abort();
    }
    close(fd);
    if(shm_unlink(name.c_str())<0){
        perror("shm_unlink");
        abort();
    }

    uint8_t* bytes = static_cast<uint8_t*>(base);
    ctl_     = reinterpret_cast<shm_ring_control*>(bytes);
    headers_ = reinterpret_cast<strip_hdr*>(bytes + sizeof(shm_ring_control));
    pixels_  = bytes + sizeof(shm_ring_control) + capacity_ * sizeof(strip_hdr);


    if(sem_init(&ctl_->empty, 1, capacity_)<0){
        perror("sem_init empty");
        abort();
    }
    if(sem_init(&ctl_->full, 1, 0)<0){
        perror("sem_init full");
        abort();
    }
}

shm_ring::~shm_ring(){
    if(sem_destroy(&(ctl_->full))<0){
        perror("sem destroy full");
    }
    if(sem_destroy(&(ctl_->empty))<0){
        perror("sem destroy empty");
    }
    if(munmap(ctl_, region_bytes_)<0){
        perror("munmap");
    }
}

strip_view shm_ring::acquire(){
    {
        scoped_timer timer(this_stage().wait_out_ns);   // stage statistics: waiting for space
        while(sem_wait(&(ctl_->empty))<0){
            if(errno==EINTR) continue;
            perror("sem_wait empty");
            abort();
        }
    }
    const size_t slot = head_ % capacity_;
    return {headers_ + slot, pixels_+slot * slot_bytes_};
}
void shm_ring::publish(){
    // Link statistics, then (only in fault-injection builds) break the strip on purpose.
    const size_t slot = head_ % capacity_;
    sent_.add(headers_[slot], (size_t)headers_[slot].nrows * width_ * 3, 0);
    maybe_inject_fault(headers_[slot], pixels_ + slot * slot_bytes_);

    ++head_;
    if(sem_post(&(ctl_->full))<0){
        perror("sem_post full");
        abort();
    }
}

strip_view shm_ring::receive(){
    {
        scoped_timer timer(this_stage().wait_in_ns);    // stage statistics: waiting for data
        while(sem_wait(&(ctl_->full))<0){
            if(errno==EINTR) continue;
            perror("sem_wait full");
            abort();
        }
    }
    if(this_stage().first_in_ns == 0) this_stage().first_in_ns = now_ns();
    const size_t slot = tail_ % capacity_;
    return {headers_ + slot, pixels_+slot * slot_bytes_};
}
void shm_ring::release(){
    // Link statistics: the stage releases a strip only after verify_strip() accepted it.
    const size_t slot = tail_ % capacity_;
    received_.add(headers_[slot], (size_t)headers_[slot].nrows * width_ * 3, 0);

    ++tail_;
    if(sem_post(&(ctl_->empty))<0){
        perror("sem_post empty");
        abort();
    }
}