#!/bin/bash

for i in $(seq 1 4)
do
    echo "Run #$i"
    ./c8
    sleep 3
done

