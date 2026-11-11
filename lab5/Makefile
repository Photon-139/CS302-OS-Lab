INPUT = 1
OUTPUT = 1_out

CXX = g++
# EXTRA: additional flags, e.g. make clean && make part2_1_variant1 EXTRA=-DFAULT_INJECT=1
CXXFLAGS = -O2 -Wall -Wextra $(EXTRA)
COMMON_SRC = ./libppm.cpp ./image_operations.cpp ./strip.cpp ./stats.cpp
COMMON_HDR = ./libppm.hpp ./image_operations.hpp ./strip.hpp ./stats.hpp ./config.hpp
RING_SRC = ./spsc_ring.cpp
RING_HDR = ./spsc_ring.hpp

PIPE_SRC = ./fd_channel.cpp ./io_utils.cpp
PIPE_HDR = ./fd_channel.hpp ./io_utils.hpp
SHM_SRC = ./shm_ring.cpp
SHM_HDR = ./shm_ring.hpp
NET_SRC = ./net_utils.cpp
NET_HDR = ./net_utils.hpp

# Part III: B listens on PORT; A connects to HOST:PORT. Override on the command line, e.g.
#   make part3_1_variant1_B PORT=12345                      (on machine B, start first)
#   make part3_1_variant1_A HOST=192.168.1.20 PORT=12345    (on machine A)
HOST ?= 127.0.0.1
PORT ?= 12345

.PHONY: part1 run-part1 part2_1_variant1 part2_1_variant2 part2_1_tsan part2_2_variant1 part2_2_variant2 part2_3 part2_3_variant1 part2_3_variant2 \
        part3_1_variant1_A part3_1_variant1_B part3_1_variant2_A part3_1_variant2_B \
        part3_2_variant1_A part3_2_variant1_B part3_2_variant2_A part3_2_variant2_B clean-part1 clean

# ---------------- Part I ----------------

part1: ./part1.out
	./part1.out ./images/$(INPUT).ppm ./part1/output_part1.ppm

./part1.out: ./part1/image_sharpener.cpp $(COMMON_SRC) $(COMMON_HDR)
	$(CXX) $(CXXFLAGS) ./part1/image_sharpener.cpp $(COMMON_SRC) -o part1.out

run-part1: ./part1.out
	mkdir -p ./images/output
	./part1.out ./images/$(INPUT).ppm ./images/output/$(OUTPUT).ppm

clean-part1:
	rm -f part1.out

# Serial baseline for speedup measurements (not a graded target): same work as the variants,
# one thread. Run via scripts/serial_baseline.sh, or directly:
#   ./serial_baseline.out <variant> <input.ppm> <output.ppm>
./serial_baseline.out: ./part1/serial_baseline.cpp $(COMMON_SRC) $(COMMON_HDR)
	$(CXX) $(CXXFLAGS) ./part1/serial_baseline.cpp $(COMMON_SRC) -o serial_baseline.out

# ---------------- Part II.1: threads + atomics ----------------

./part2_1.out: ./part2/part2_1.cpp $(COMMON_SRC) $(COMMON_HDR) $(RING_SRC) $(RING_HDR)
	$(CXX) $(CXXFLAGS) -pthread ./part2/part2_1.cpp $(COMMON_SRC) $(RING_SRC) -o part2_1.out

part2_1_variant1: ./part2_1.out
	./part2_1.out 1 ./images/$(INPUT).ppm ./part2/output_part2_1_variant1.ppm

part2_1_variant2: ./part2_1.out
	./part2_1.out 2 ./images/$(INPUT).ppm ./part2/output_part2_1_variant2.ppm

# ThreadSanitizer build for testing (not part of the submission targets).
# setarch -R disables address randomisation, which TSan needs on recent kernels.
./part2_1_tsan.out: ./part2/part2_1.cpp $(COMMON_SRC) $(COMMON_HDR) $(RING_SRC) $(RING_HDR)
	$(CXX) -O1 -g -fsanitize=thread -pthread ./part2/part2_1.cpp $(COMMON_SRC) $(RING_SRC) -o part2_1_tsan.out

part2_1_tsan: ./part2_1_tsan.out
	setarch $$(uname -m) -R ./part2_1_tsan.out 1 ./images/$(INPUT).ppm ./part2/output_part2_1_tsan.ppm
	setarch $$(uname -m) -R ./part2_1_tsan.out 2 ./images/$(INPUT).ppm ./part2/output_part2_1_tsan.ppm

# ---------------- Part II.2: processes + pipes ----------------

./part2_2.out: ./part2/part2_2.cpp $(COMMON_SRC) $(COMMON_HDR) $(PIPE_SRC) $(PIPE_HDR)
	$(CXX) $(CXXFLAGS) ./part2/part2_2.cpp $(COMMON_SRC) $(PIPE_SRC) -o part2_2.out

part2_2_variant1: ./part2_2.out
	./part2_2.out 1 ./images/$(INPUT).ppm ./part2/output_part2_2_variant1.ppm

part2_2_variant2: ./part2_2.out
	./part2_2.out 2 ./images/$(INPUT).ppm ./part2/output_part2_2_variant2.ppm

# ---------------- Part II.3: processes + shared memory + semaphores ----------------
# -pthread: POSIX semaphores. On glibc older than 2.34, also add -lrt for shm_open.

./part2_3.out: ./part2/part2_3.cpp $(COMMON_SRC) $(COMMON_HDR) $(SHM_SRC) $(SHM_HDR)
	$(CXX) $(CXXFLAGS) -pthread ./part2/part2_3.cpp $(COMMON_SRC) $(SHM_SRC) -o part2_3.out

part2_3_variant1: ./part2_3.out
	./part2_3.out 1 ./images/$(INPUT).ppm ./part2/output_part2_3_variant1.ppm

part2_3_variant2: ./part2_3.out
	./part2_3.out 2 ./images/$(INPUT).ppm ./part2/output_part2_3_variant2.ppm

# The assignment lists this target without a variant: run both.
part2_3: part2_3_variant1 part2_3_variant2

# ---------------- Part III.1: A = S1 + S2 (threads), B = S3, over TCP ----------------
# B takes k from A; B is given the variant too and rejects a run where A's variant differs.

./part3_1_A.out: ./part3/part3_1_A.cpp $(COMMON_SRC) $(COMMON_HDR) $(RING_SRC) $(RING_HDR) $(PIPE_SRC) $(PIPE_HDR) $(NET_SRC) $(NET_HDR)
	$(CXX) $(CXXFLAGS) -pthread ./part3/part3_1_A.cpp $(COMMON_SRC) $(RING_SRC) $(PIPE_SRC) $(NET_SRC) -o part3_1_A.out

./part3_1_B.out: ./part3/part3_1_B.cpp $(COMMON_SRC) $(COMMON_HDR) $(PIPE_SRC) $(PIPE_HDR) $(NET_SRC) $(NET_HDR)
	$(CXX) $(CXXFLAGS) -pthread ./part3/part3_1_B.cpp $(COMMON_SRC) $(PIPE_SRC) $(NET_SRC) -o part3_1_B.out

part3_1_variant1_A: ./part3_1_A.out
	./part3_1_A.out 1 ./images/$(INPUT).ppm $(HOST) $(PORT)

part3_1_variant1_B: ./part3_1_B.out
	./part3_1_B.out 1 ./images/$(INPUT).ppm ./part3/output_part3_1_variant1.ppm $(PORT)

part3_1_variant2_A: ./part3_1_A.out
	./part3_1_A.out 2 ./images/$(INPUT).ppm $(HOST) $(PORT)

part3_1_variant2_B: ./part3_1_B.out
	./part3_1_B.out 2 ./images/$(INPUT).ppm ./part3/output_part3_1_variant2.ppm $(PORT)

# ---------------- Part III.2: A = S1, B = S2 + S3 (threads), over TCP ----------------
# B takes k and the ring capacity from A; B is given the variant too and rejects a run where
# A's variant differs.

./part3_2_A.out: ./part3/part3_2_A.cpp $(COMMON_SRC) $(COMMON_HDR) $(PIPE_SRC) $(PIPE_HDR) $(NET_SRC) $(NET_HDR)
	$(CXX) $(CXXFLAGS) -pthread ./part3/part3_2_A.cpp $(COMMON_SRC) $(PIPE_SRC) $(NET_SRC) -o part3_2_A.out

./part3_2_B.out: ./part3/part3_2_B.cpp $(COMMON_SRC) $(COMMON_HDR) $(RING_SRC) $(RING_HDR) $(PIPE_SRC) $(PIPE_HDR) $(NET_SRC) $(NET_HDR)
	$(CXX) $(CXXFLAGS) -pthread ./part3/part3_2_B.cpp $(COMMON_SRC) $(RING_SRC) $(PIPE_SRC) $(NET_SRC) -o part3_2_B.out

part3_2_variant1_A: ./part3_2_A.out
	./part3_2_A.out 1 ./images/$(INPUT).ppm $(HOST) $(PORT)

part3_2_variant1_B: ./part3_2_B.out
	./part3_2_B.out 1 ./images/$(INPUT).ppm ./part3/output_part3_2_variant1.ppm $(PORT)

part3_2_variant2_A: ./part3_2_A.out
	./part3_2_A.out 2 ./images/$(INPUT).ppm $(HOST) $(PORT)

part3_2_variant2_B: ./part3_2_B.out
	./part3_2_B.out 2 ./images/$(INPUT).ppm ./part3/output_part3_2_variant2.ppm $(PORT)

# ---------------- cleanup ----------------

clean:
	rm -f *.out
