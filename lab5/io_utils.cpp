#include "io_utils.hpp"
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

// Read exactly n bytes. read() may return fewer bytes than asked for (pipes, sockets),
// so keep going, advancing through the buffer.
// Returns false if EOF is reached before n bytes arrived (the writer closed its end).
// Any other error is fatal.
bool readn(int fd, void* buf, size_t n){
    uint8_t* p = static_cast<uint8_t*>(buf);
    size_t bytes_left = n;
    while(bytes_left>0){
        ssize_t bytes_read = read(fd, p, bytes_left);
        if(bytes_read < 0){
            if(errno == EINTR) continue;    // interrupted by a signal before reading anything: retry
            perror("readn: read");
            abort();
        }
        if(bytes_read == 0){
            return false;                   // EOF: no writer left
        }
        p += bytes_read;
        bytes_left -= (size_t)bytes_read;
    }
    return true;
}

// Write exactly n bytes. write() may also accept fewer bytes than given,
// so keep going, advancing through the buffer.
// Any error is fatal. If the reader has exited, write() fails with EPIPE (when SIGPIPE
// is ignored) or the process is killed by SIGPIPE (default); either way it stops here.
bool writen(int fd, const void* buf, size_t n){
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    size_t bytes_left = n;
    while(bytes_left>0){
        ssize_t bytes_written = write(fd, p, bytes_left);
        if(bytes_written < 0){
            if(errno == EINTR) continue;    // interrupted by a signal before writing anything: retry
            perror("writen: write");
            abort();
        }
        p += bytes_written;
        bytes_left -= (size_t)bytes_written;
    }
    return true;
}
