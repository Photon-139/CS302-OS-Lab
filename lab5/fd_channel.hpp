#pragma once
#include "strip.hpp"

class fd_channel {
public:
    fd_channel(int read_fd, int write_fd, int k, int width);   // pipes: the two ends; sockets: same fd twice
    ~fd_channel();                                             // frees the buffer (fds are closed by the owner)

    fd_channel(const fd_channel&)            = delete;
    fd_channel& operator=(const fd_channel&) = delete;

    // Producer
    strip_view acquire();   // returns {&hdr_, buf_}; never waits
    void       publish();   // writen(header), then writen(hdr_.nrows × width × 3 pixel bytes)

    // Consumer
    strip_view receive();   // readn(header); abort if nrows > k; readn(payload) into buf_
    void       release();   // no-op

private:
    int       read_fd_, write_fd_, k_, width_;
    strip_hdr hdr_;
    uint8_t*  buf_;         // k × width × 3 bytes
};
