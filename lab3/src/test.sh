#!/bin/bash

algorithms=("FIFO" "RR-2" "MLFQ" "MLFQ-BOOST")


for algo in "${algorithms[@]}"; do
    for test in {1..5}; do
        ./schedule.out "$algo" "../workload/process${test}.txt" "1" >> output_1.txt
    done 
done

for algo in "${algorithms[@]}"; do
    for test in {1..5}; do
        ./schedule.out "$algo" "../workload/process${test}.txt" "2" >> output_2.txt
    done 
done
