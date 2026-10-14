#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

// Streams addresses out of a trace file without loading the whole file.
// Each line is "<decimal address>,<I/D>,<R/W>"; the first line is a header and is skipped.
// Addresses can be wider than 32 bits, so only the lower 32 bits are returned.
class TraceReader{
    FILE* file;
    std::vector<char> buffer;       // chunk of the file currently being parsed
    size_t pos = 0;                 // next unread byte in buffer
    size_t len = 0;                 // number of valid bytes in buffer

    bool refill();                  // reads the next chunk; false at end of file
    int next_char();                // next byte, or -1 at end of file

public:
    explicit TraceReader(const char* path);
    ~TraceReader();
    TraceReader(const TraceReader&) = delete;
    TraceReader& operator=(const TraceReader&) = delete;

    // Stores the next access's address in `address`. Returns false when the trace is finished.
    bool next(uint32_t& address);
};
