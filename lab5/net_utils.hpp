#pragma once
#include <cstdint>

// Socket setup and the small control messages exchanged between machine A and machine B
// in Part III. Strips themselves go through fd_channel, using the connected socket fd.
//
// Connection: B (downstream, writes the output) is the server and is started first;
// A connects to it. Any setup error prints a message and exits.

// ---- Connection ----

// B: listen on `port` (all interfaces) and accept exactly one connection.
// Returns the connected socket fd; the listening socket is closed.
int listen_on(int port);

// A: connect to `host` (name or IP address) on `port`. Retries for about 10 seconds,
// so A may be started slightly before B is listening.
int connect_to(const char* host, int port);

// Disable Nagle's algorithm: strips (and the small headers) are sent immediately
// instead of being held back to batch them with later data.
void set_nodelay(int fd);

// ---- Run parameters (A -> B, once, right after connecting) ----

constexpr uint32_t RUN_PARAMS_MAGIC = 0x676767;   // "LAB5": guards against talking to something else

// Everything B needs to run its stages exactly like A. B takes these from A
// (instead of its own command line), so the two machines cannot disagree.
struct run_params {
    uint32_t magic;        // RUN_PARAMS_MAGIC
    uint32_t width;        // of A's input image; B checks against its own copy
    uint32_t height;
    uint32_t k;            // rows per strip
    uint32_t variant;      // 1 or 2
    uint32_t iterations;   // config::ITERATIONS on A
    uint32_t passes;       // config::PASSES_PER_RESULT on A
    uint32_t ring_capacity; // N for whichever machine has the ring (A in III.1, B in III.2)
};

// A: send the parameters (each field in network byte order).
void send_params(int fd, const run_params& p);

// B: receive the parameters; exits if the connection ends early or the magic is wrong.
run_params recv_params(int fd);

// ---- Completion (B -> A, once, after B has written the output file) ----

// B: tell A that the whole pipeline has finished.
void send_done(int fd);

// A: block until B reports completion. Lets A measure the end-to-end time with its own clock
// (the two machines' clocks are not synchronised).
void wait_done(int fd);
