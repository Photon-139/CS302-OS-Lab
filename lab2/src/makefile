build-part1: part1.out

part1.out: part1_searcher.cpp
	g++ -g part1_searcher.cpp -o part1.out

run-part1: part1.out
	./part1.out file.txt NGTNIJGK 0 67108863

clean-part1:
	rm part1.out

clean:
	rm *.out

build-part2: part2.out part2_searcher.out

part2.out: part2_partitioner.cpp
	g++ -g part2_partitioner.cpp -o part2.out

part2_searcher.out: part2_searcher.cpp
	g++ -g part2_searcher.cpp -o part2_searcher.out


run-part2: build-part2
	./part2.out file.txt NGTNIJGK 0 67108863 8388608

clean-part2:
	rm part2.out part2_searcher.out
