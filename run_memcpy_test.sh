#!/bin/zsh
for((i=1;i<=20;i++));
do
    echo "#"$i >> ret_mem_1;
    ./memcpy_test bigfile 1 >> ret_mem_1;
done

for((i=1;i<=20;i++));
do
    echo "#"$i >> ret_mem_2;
    ./memcpy_test bigfile 2 >> ret_mem_2;
done
