#!/bin/bash

quantum=(2 4 6 8 10 16)

> cpu_1.txt
> cpu_2.txt

for cpu in {1..2}; do
    for q in "${quantum[@]}"; do
        for test in {1..5}; do
            ./schedule.out "RR-$q" "../workload/process$test.txt" "$cpu" >> "rr_$cpu.txt"
        done
    done
done