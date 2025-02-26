#!/bin/bash

make 

sudo insmod get_pgfaults.ko 

echo "This command gives pagefault count using get_pgfaults kernel module"
cat /proc/get_pgfaults 

echo "This command gives pagefault count using inbuild kernel module"
cat /proc/vmstat| grep pgfault

sudo rmmod get_pgfaults
make clean