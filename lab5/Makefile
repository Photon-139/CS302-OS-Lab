INPUT = 1
OUTPUT = 1_out

part1: ./part1.out
	./part1.out ./images/$(INPUT).ppm ./part1/output_part1.ppm

./part1.out: ./part1/image_sharpener.cpp ./libppm.cpp
	g++ -g ./part1/image_sharpener.cpp ./libppm.cpp -o part1.out

run-part1: ./part1.out
	./part1.out ./images/$(INPUT).ppm ./images/output/$(OUTPUT).ppm

clean-part1:
	rm part1.out

clean:
	rm *.out
