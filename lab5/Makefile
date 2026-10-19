INPUT = 1
OUTPUT = 1_out

build-part1: ./part1.out

./part1.out: ./part1/image_sharpener.cpp ./part1/libppm.cpp
	g++ -g ./part1/image_sharpener.cpp ./part1/libppm.cpp -o part1.out

run-part1: ./part1.out
	./part1.out ./images/$(INPUT).ppm ./images/output/$(OUTPUT).ppm

clean-part1:
	rm part1.out

clean:
	rm *.out
