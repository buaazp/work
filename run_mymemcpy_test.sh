#!/bin/zsh
for((i=1;i<=20;i++));
do
    echo "#"$i >> ret_mymem_1;
    ./mymemcpy random >> ret_mymem_1;
done

