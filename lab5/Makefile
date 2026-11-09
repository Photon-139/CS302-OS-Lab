INPUT = 1
OUTPUT = 1_out

CXX = g++
CXXFLAGS = -O2 -Wall -Wextra
COMMON_SRC = ./libppm.cpp ./image_operations.cpp ./strip.cpp
COMMON_HDR = ./libppm.h ./image_operations.hpp ./strip.hpp

part1: ./part1.out
	./part1.out ./images/$(INPUT).ppm ./part1/output_part1.ppm

./part1.out: ./part1/image_sharpener.cpp $(COMMON_SRC) $(COMMON_HDR)
	$(CXX) $(CXXFLAGS) ./part1/image_sharpener.cpp $(COMMON_SRC) -o part1.out

run-part1: ./part1.out
	mkdir -p ./images/output
	./part1.out ./images/$(INPUT).ppm ./images/output/$(OUTPUT).ppm

clean-part1:
	rm part1.out

clean:
	rm *.out
