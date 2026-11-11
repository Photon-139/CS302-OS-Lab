#include "fd_channel.hpp"
#include "io_utils.hpp"
#include "libppm.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>

fd_channel::fd_channel(int read_fd, int write_fd, int k, int width)
: read_fd_(read_fd), write_fd_(write_fd), k_(k), width_(width),
  buf_(new uint8_t[(size_t)k * width * CHANNELS])
{}

fd_channel::~fd_channel(){
    delete[] buf_;
}

// ---- Producer ----

// The strip is filled in this process's own header and buffer; nothing to wait for,
// because publish() copies it into the pipe/socket before the next acquire().
strip_view fd_channel::acquire(){
    assert(write_fd_ >= 0);
    return {&hdr_, buf_};
}

// Header first, then exactly nrows rows of pixels: the receiver reads the header to know
// how many payload bytes follow, which keeps the byte stream in step (also for a shorter last strip).
void fd_channel::publish(){
    assert(write_fd_ >= 0);
    const size_t payload = (size_t)hdr_.nrows * width_ * CHANNELS;

    // Link statistics, then (only in fault-injection builds) break the strip on purpose.
    sent_.add(hdr_, payload, WIRE_HDR_BYTES + payload);
    maybe_inject_fault(hdr_, buf_);

    // Writing blocks while the pipe/socket buffer is full: counted as waiting for space.
    scoped_timer timer(this_stage().wait_out_ns);
    // Header in network byte order, so a machine with a different byte order reads it correctly.
    uint8_t wire[WIRE_HDR_BYTES];
    encode_header(hdr_, wire);
    writen(write_fd_, wire, WIRE_HDR_BYTES);

    // Pixels are single bytes: byte order doesn't apply, send as they are.
    writen(write_fd_, buf_, payload);
}

// ---- Consumer ----

// Reads the next strip into this process's own header and buffer.
// EOF here is always an error: every stage knows exactly how many strips to expect,
// so the stream can only end early if the producer died.
strip_view fd_channel::receive(){
    assert(read_fd_ >= 0);
    // Reading blocks until data arrives: counted as waiting for data (includes the transfer).
    scoped_timer timer(this_stage().wait_in_ns);

    uint8_t wire[WIRE_HDR_BYTES];
    if(!readn(read_fd_, wire, WIRE_HDR_BYTES)){
        fprintf(stderr, "fd_channel: unexpected end of stream while reading a strip header\n");
        abort();
    }
    decode_header(wire, hdr_);          // back to this machine's byte order

    if(hdr_.nrows == 0 || hdr_.nrows > (uint32_t)k_){
        fprintf(stderr, "fd_channel: received strip with nrows=%u, expected 1..%d\n", hdr_.nrows, k_);
        abort();
    }

    if(!readn(read_fd_, buf_, (size_t)hdr_.nrows * width_ * CHANNELS)){
        fprintf(stderr, "fd_channel: unexpected end of stream while reading strip (r0=%u, nrows=%u)\n",
                hdr_.r0, hdr_.nrows);
        abort();
    }
    if(this_stage().first_in_ns == 0) this_stage().first_in_ns = now_ns();
    return {&hdr_, buf_};
}


// Nothing to give back: the data was already copied out of the pipe/socket by receive().
// Only counts the strip (the stage releases it after verify_strip() accepted it).
void fd_channel::release(){
    const size_t payload = (size_t)hdr_.nrows * width_ * CHANNELS;
    received_.add(hdr_, payload, WIRE_HDR_BYTES + payload);
}
