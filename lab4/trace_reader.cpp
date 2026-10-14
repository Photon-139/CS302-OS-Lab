#include "trace_reader.hpp"
#include <cstdlib>
#include <iostream>

constexpr size_t CHUNK_SIZE = 1 << 20;

TraceReader::TraceReader(const char* path) : buffer(CHUNK_SIZE){
    file = fopen(path, "rb");
    if(!file){
        std::cerr << "Could not open trace file: " << path << std::endl;
        exit(1);
    }
    // Skip the header line.
    int c;
    while((c = next_char()) != -1 && c != '\n'){}
}

TraceReader::~TraceReader(){
    fclose(file);
}

bool TraceReader::refill(){
    len = fread(buffer.data(), 1, buffer.size(), file);
    pos = 0;
    return len > 0;
}

int TraceReader::next_char(){
    if(pos == len && !refill()) return -1;
    return static_cast<unsigned char>(buffer[pos++]);
}

bool TraceReader::next(uint32_t& address){
    // Skip anything before the first digit (blank lines, '\r').
    int c = next_char();
    while(c != -1 && (c < '0' || c > '9')){
        c = next_char();
    }
    if(c == -1) return false;

    uint64_t value = 0;
    while(c >= '0' && c <= '9'){
        value = value * 10 + (c - '0');
        c = next_char();
    }
    // c is now ',', so throw away the rest of the line.
    while(c != -1 && c != '\n'){
        c = next_char();
    }
    address = static_cast<uint32_t>(value);     // keep the lower 32 bits
    return true;
}
