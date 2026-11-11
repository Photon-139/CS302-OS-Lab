#include "net_utils.hpp"
#include "io_utils.hpp"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Setup errors are not bugs (wrong address, port in use, B not running, ...): report and exit.
[[noreturn]] static void die(const char* what)
{
    perror(what);
    exit(1);
}

// ---- Connection ----

int listen_on(int port)
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_fd < 0) die("socket");

    // Allow restarting immediately on the same port (otherwise bind fails for ~60 s
    // after the previous run while the old connection is in TIME_WAIT).
    int yes = 1;
    if(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) die("setsockopt SO_REUSEADDR");

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);     // all interfaces
    addr.sin_port        = htons((uint16_t)port);
    if(bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) die("bind");
    if(listen(listen_fd, 1) < 0) die("listen");

    fprintf(stderr, "waiting for a connection on port %d ...\n", port);

    int fd;
    while((fd = accept(listen_fd, nullptr, nullptr)) < 0){
        if(errno == EINTR) continue;
        die("accept");
    }
    close(listen_fd);                               // exactly one connection is needed
    return fd;
}

int connect_to(const char* host, int port)
{
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;                  // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    std::string port_str = std::to_string(port);
    int err = getaddrinfo(host, port_str.c_str(), &hints, &results);
    if(err != 0){
        fprintf(stderr, "getaddrinfo(%s): %s\n", host, gai_strerror(err));
        exit(1);
    }

    // B may not be listening yet: retry every 200 ms for about 10 seconds.
    const int attempts = 50;
    for(int attempt = 0; attempt < attempts; ++attempt){
        for(addrinfo* a = results; a != nullptr; a = a->ai_next){
            int fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if(fd < 0) continue;
            if(connect(fd, a->ai_addr, a->ai_addrlen) == 0){
                freeaddrinfo(results);
                return fd;
            }
            close(fd);
        }
        usleep(200 * 1000);
    }

    freeaddrinfo(results);
    fprintf(stderr, "could not connect to %s:%d (is B running, is the port open in the firewall?)\n", host, port);
    exit(1);
}

void set_nodelay(int fd)
{
    int yes = 1;
    if(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) < 0) die("setsockopt TCP_NODELAY");
}

// ---- Run parameters ----

static constexpr int RUN_PARAMS_WORDS = 10;

void send_params(int fd, const run_params& p)
{
    const uint32_t words[RUN_PARAMS_WORDS] = {
        htonl(p.magic), htonl(p.width), htonl(p.height), htonl(p.k),
        htonl(p.variant), htonl(p.iterations), htonl(p.passes), htonl(p.ring_capacity),
        htonl(p.image_hash_hi), htonl(p.image_hash_lo),
    };
    writen(fd, words, sizeof(words));
}

run_params recv_params(int fd)
{
    uint32_t words[RUN_PARAMS_WORDS];
    if(!readn(fd, words, sizeof(words))){
        fprintf(stderr, "connection closed before the run parameters arrived\n");
        exit(1);
    }

    run_params p;
    p.magic      = ntohl(words[0]);
    p.width      = ntohl(words[1]);
    p.height     = ntohl(words[2]);
    p.k          = ntohl(words[3]);
    p.variant    = ntohl(words[4]);
    p.iterations = ntohl(words[5]);
    p.passes     = ntohl(words[6]);
    p.ring_capacity = ntohl(words[7]);
    p.image_hash_hi = ntohl(words[8]);
    p.image_hash_lo = ntohl(words[9]);

    if(p.magic != RUN_PARAMS_MAGIC){
        fprintf(stderr, "unexpected data from peer (bad magic %08x): is the other side this program?\n", p.magic);
        exit(1);
    }
    return p;
}

// ---- Completion ----

static constexpr uint8_t DONE_BYTE = 'D';

void send_done(int fd)
{
    writen(fd, &DONE_BYTE, 1);
}

void wait_done(int fd)
{
    uint8_t byte = 0;
    if(!readn(fd, &byte, 1)){
        fprintf(stderr, "connection closed before B reported completion (did B fail?)\n");
        exit(1);
    }
    if(byte != DONE_BYTE){
        fprintf(stderr, "unexpected completion message from B: %u\n", byte);
        exit(1);
    }
}
